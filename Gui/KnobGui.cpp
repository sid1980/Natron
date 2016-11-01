/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****


#include "KnobGui.h"

#include <cassert>
#include <stdexcept>

#include <QUndoCommand>
#include <QMessageBox>

#include <boost/weak_ptr.hpp>

#include "Engine/KnobTypes.h"
#include "Engine/KnobItemsTable.h"
#include "Engine/EffectInstance.h"
#include "Engine/ViewIdx.h"

#include "Gui/KnobGuiPrivate.h"
#include "Gui/GuiDefines.h"
#include "Gui/KnobGuiFactory.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/ClickableLabel.h"


NATRON_NAMESPACE_ENTER;


/////////////// KnobGui
KnobGui::KnobGui(const KnobIPtr& knob,
                 KnobLayoutTypeEnum layoutType,
                 KnobGuiContainerI* container)
    : QObject()
    , KnobGuiI()
    , boost::enable_shared_from_this<KnobGui>()
    , _imp( new KnobGuiPrivate(this, knob, layoutType, container) )
{
}

KnobGui::~KnobGui()
{
}

KnobIPtr
KnobGui::getKnob() const
{
    return _imp->knob.lock();
}

void
KnobGui::initialize()
{
    KnobIPtr knob = getKnob();
    KnobGuiPtr thisShared = shared_from_this();

    assert(thisShared);

    // Set the pointer to the GUI only for the settings panel knob gui
    NodeSettingsPanel* isNodePanel = dynamic_cast<NodeSettingsPanel*>(_imp->container);
    if (isNodePanel) {
        knob->setKnobGuiPointer(thisShared);
    }
    KnobHelperPtr helper = toKnobHelper(knob);
    assert(helper);
    if (helper) {
        KnobSignalSlotHandler* handler = helper->getSignalSlotHandler().get();
        QObject::connect( handler, SIGNAL(mustRefreshKnobGui(ViewSetSpec,DimSpec,ValueChangedReasonEnum)), this, SLOT(onInternalValueChanged(ViewSetSpec,DimSpec,ValueChangedReasonEnum)) );
        QObject::connect( handler, SIGNAL(curveAnimationChanged(std::list<double>,std::list<double>,ViewIdx,DimIdx)), this, SLOT(onCurveAnimationChangedInternally(std::list<double>,std::list<double>,ViewIdx,DimIdx)));
        QObject::connect( handler, SIGNAL(secretChanged()), this, SLOT(setSecret()) );
        QObject::connect( handler, SIGNAL(enabledChanged()), this, SLOT(setEnabledSlot()) );
        QObject::connect( handler, SIGNAL(dirty(bool)), this, SLOT(onSetDirty(bool)) );
        QObject::connect( handler, SIGNAL(animationLevelChanged(ViewSetSpec,DimSpec)), this, SLOT(onAnimationLevelChanged(ViewSetSpec,DimSpec)) );
        QObject::connect( handler, SIGNAL(appendParamEditChange(ValueChangedReasonEnum,ValueChangedReturnCodeEnum,Variant,ViewSetSpec,DimSpec,double,bool)), this, SLOT(onAppendParamEditChanged(ValueChangedReasonEnum,ValueChangedReturnCodeEnum,Variant,ViewSetSpec,DimSpec,double,bool)) );
        QObject::connect( handler, SIGNAL(frozenChanged(bool)), this, SLOT(onFrozenChanged(bool)) );
        QObject::connect( handler, SIGNAL(helpChanged()), this, SLOT(onHelpChanged()) );
        QObject::connect( handler, SIGNAL(expressionChanged(DimIdx,ViewIdx)), this, SLOT(onExprChanged(DimIdx,ViewIdx)) );
        QObject::connect( handler, SIGNAL(hasModificationsChanged()), this, SLOT(onHasModificationsChanged()) );
        QObject::connect( handler, SIGNAL(labelChanged()), this, SLOT(onLabelChanged()) );
        QObject::connect( handler, SIGNAL(dimensionNameChanged(DimIdx)), this, SLOT(onDimensionNameChanged(DimIdx)) );
        QObject::connect( handler, SIGNAL(viewerContextSecretChanged()), this, SLOT(onViewerContextSecretChanged()) );
    }

}

KnobGuiContainerI*
KnobGui::getContainer()
{
    return _imp->container;
}

void
KnobGui::removeGui()
{
    assert(!_imp->guiRemoved);
    for (std::vector< boost::weak_ptr< KnobI > >::iterator it = _imp->knobsOnSameLine.begin(); it != _imp->knobsOnSameLine.end(); ++it) {
        KnobGuiPtr kg = _imp->container->getKnobGui( it->lock() );
        if (kg) {
            kg->_imp->removeFromKnobsOnSameLineVector( getKnob() );
        }
    }

    if ( _imp->knobsOnSameLine.empty() ) {
        if (_imp->isOnNewLine) {
            if (_imp->labelContainer) {
                _imp->labelContainer->deleteLater();
                _imp->labelContainer = 0;
            }
        }
    } else {
        if (_imp->descriptionLabel) {
            _imp->descriptionLabel->deleteLater();
            _imp->descriptionLabel = 0;
        }
    }
    for (KnobGuiPrivate::PerViewWidgetsMap::iterator it = _imp->views.begin(); it != _imp->views.end(); ++it) {
        if (_imp->knobsOnSameLine.empty() && _imp->isOnNewLine && it->second.field) {
            it->second.field->deleteLater();
            it->second.field = 0;
        }
        if (it->second.widgets) {
            it->second.widgets->removeSpecificGui();
        }
    }
    _imp->guiRemoved = true;
}

void
KnobGui::setGuiRemoved()
{
    _imp->guiRemoved = true;
}

Gui*
KnobGui::getGui() const
{
    return _imp->container->getGui();
}

const QUndoCommand*
KnobGui::getLastUndoCommand() const
{
    return _imp->container->getLastUndoCommand();
}

void
KnobGui::pushUndoCommand(QUndoCommand* cmd)
{
    if ( getKnob()->getCanUndo() && getKnob()->getEvaluateOnChange() ) {
        _imp->container->pushUndoCommand(cmd);
    } else {
        cmd->redo();
        delete cmd;
    }
}

/**
 * @brief Given a knob "ref" within a vector of knobs, returns a vector
 * of all knobs that should be on the same horizontal line as this knob.
 **/
static void
findKnobsOnSameLine(const KnobsVec& knobs,
                    const KnobIPtr& ref,
                    std::vector<KnobIWPtr>& knobsOnSameLine,
                    KnobIPtr* prevKnob)
{
    int idx = -1;

    for (U32 k = 0; k < knobs.size(); ++k) {
        if (knobs[k] == ref) {
            idx = k;
            if (k > 0) {
                *prevKnob = knobs[k - 1];
            }
            break;
        }
    }
    assert(idx != -1);
    if (idx < 0) {
        return;
    }
    ///find all knobs backward that are on the same line.
    int k = idx - 1;
    KnobIPtr parent = ref->getParentKnob();

    while ( k >= 0 && !knobs[k]->isNewLineActivated() ) {
        if (parent) {
            assert(knobs[k]->getParentKnob() == parent);
            knobsOnSameLine.push_back(knobs[k]);
        } else {
            if ( !knobs[k]->getParentKnob() &&
                !toKnobPage( knobs[k] ) &&
                !toKnobGroup( knobs[k] ) ) {
                knobsOnSameLine.push_back(knobs[k]);
            }
        }
        --k;
    }

    ///find all knobs forward that are on the same line.
    k = idx;
    while ( k < (int)(knobs.size() - 1) && !knobs[k]->isNewLineActivated() ) {
        if (parent) {
            assert(knobs[k + 1]->getParentKnob() == parent);
            knobsOnSameLine.push_back(knobs[k + 1]);
        } else {
            if ( !knobs[k + 1]->getParentKnob() &&
                !toKnobPage( knobs[k + 1] ) &&
                !toKnobGroup( knobs[k + 1] ) ) {
                knobsOnSameLine.push_back(knobs[k + 1]);
            }
        }
        ++k;
    }
} // findKnobsOnSameLine

void
KnobGuiPrivate::refreshIsOnNewLineFlag()
{
    KnobIPtr  k = knob.lock();
    // Find all knobs on the same layout line
    KnobIPtr parentKnob = k->getParentKnob();
    KnobGroupPtr isParentGroup = toKnobGroup(parentKnob);
    KnobIPtr prevKnobOnLine;
    if (isParentGroup) {
        // If the parent knob is a group, knobs on the same line have to be found in the children
        // of the parent
        KnobsVec groupsiblings = isParentGroup->getChildren();
        findKnobsOnSameLine(groupsiblings, k, knobsOnSameLine, &prevKnobOnLine);
    } else {
        // Parent is a page, find the siblings in the children of the page
        KnobPagePtr isParentPage = toKnobPage(parentKnob);
        if (isParentPage) {
            KnobsVec pagesiblings = isParentPage->getChildren();
            findKnobsOnSameLine(pagesiblings, k, knobsOnSameLine, &prevKnobOnLine);
        }
    }
    prevKnob = prevKnobOnLine;


    isOnNewLine = !prevKnobOnLine || prevKnobOnLine->isNewLineActivated() || k->getViewsList().size() > 1;
} // refreshIsOnNewLineFlag

bool
KnobGui::isLabelOnSameColumn() const
{
    // If createGui has not been called yet, return false
    if (_imp->views.empty()) {
        return false;
    }
    KnobGuiWidgetsPtr widgets = _imp->views.begin()->second.widgets;
    if (!widgets) {
        return false;
    }
    return widgets->isLabelOnSameColumn();

}

QWidget*
KnobGui::getFieldContainer(ViewIdx view) const
{
    KnobGuiPrivate::PerViewWidgetsMap::const_iterator foundView = _imp->views.find(view);
    if (foundView == _imp->views.end()) {
        return 0;
    }
    return foundView->second.field;
}

QWidget*
KnobGui::getLabelContainer(ViewIdx view) const
{
    if (view == ViewIdx(0)) {
        return _imp->labelContainer;
    }
    KnobGuiPrivate::PerViewWidgetsMap::const_iterator foundView = _imp->views.find(view);
    if (foundView == _imp->views.end()) {
        return 0;
    }
    return foundView->second.viewLabel;
}


KnobGuiWidgetsPtr
KnobGui::getWidgetsForView(ViewIdx view)
{
    KnobGuiPrivate::PerViewWidgetsMap::const_iterator foundView = _imp->views.find(view);
    if (foundView == _imp->views.end()) {
        return KnobGuiWidgetsPtr();
    }
    return foundView->second.widgets;
}

void
KnobGui::createViewWidgets(QWidget* parentWidget, ViewIdx view)
{
    KnobGuiPtr thisShared = shared_from_this();

    KnobGuiPrivate::ViewWidgets& viewWidgets = _imp->views[view];

    if (_imp->isOnNewLine) {
        // Create a new container and layout
        viewWidgets.field = _imp->container->createKnobHorizontalFieldContainer(parentWidget);
        viewWidgets.fieldLayout = new QHBoxLayout(viewWidgets.field);
        viewWidgets.fieldLayout->setContentsMargins( TO_DPIX(3), 0, 0, TO_DPIY(NATRON_SETTINGS_VERTICAL_SPACING_PIXELS) );
        viewWidgets.fieldLayout->setSpacing( TO_DPIY(2) );
        viewWidgets.fieldLayout->setAlignment(Qt::AlignLeft);
    } else {
        KnobIPtr prevKnob = _imp->prevKnob.lock();
        assert(prevKnob);
        KnobGuiPtr prevKnobGui = _imp->container->getKnobGui(prevKnob);
        assert(prevKnobGui);

        // Otherwise re-use the last row widget and layout
        viewWidgets.field = prevKnobGui->getFieldContainer(ViewIdx(0));
        viewWidgets.fieldLayout = dynamic_cast<QHBoxLayout*>( viewWidgets.field->layout() );
    }


    KnobIPtr knob = _imp->knob.lock();

    // Create custom interact if any. For parametric knobs, the interact is used to draw below the curves
    // not to replace entirely the widget
    KnobParametricPtr isParametric = toKnobParametric(knob);
    OfxParamOverlayInteractPtr customInteract = knob->getCustomInteract();
    if (customInteract && !isParametric) {
        _imp->customInteract = new CustomParamInteract(shared_from_this(), knob->getOfxParamHandle(), customInteract);
        viewWidgets.fieldLayout->addWidget(_imp->customInteract);
    } else {
        KnobGuiWidgetsPtr widgets(appPTR->createGuiForKnob(thisShared, view));
        widgets->createWidget(viewWidgets.fieldLayout);
        widgets->updateToolTip();

        if ( knob->isNewLineActivated() && widgets->shouldAddStretch() ) {
            viewWidgets.fieldLayout->addStretch();
        }
    }

    

} // createViewWidgets

void
KnobGuiPrivate::createLabel(QWidget* parentWidget)
{

    assert(!views.empty());
    KnobGuiWidgetsPtr firstViewWidgets = views.begin()->second.widgets;

    KnobIPtr k = knob.lock();
    std::string labelText,labelIconFilePath;
    if (layoutType == KnobGui::eKnobLayoutTypeViewerUI) {
        labelText = k->getInViewerContextLabel();
        labelIconFilePath = k->getInViewerContextIconFilePath(false);
    } else {
        if (!views.empty()) {
            // Use a label based on the implementation
            labelText = firstViewWidgets->getDescriptionLabel();
        }
        labelIconFilePath = k->getIconLabel();
    }

    QHBoxLayout* labelLayout = 0;
    if (isOnNewLine) {
        labelContainer = new QWidget(parentWidget);
        labelLayout = new QHBoxLayout(labelContainer);
        double verticalMargin = layoutType == KnobGui::eKnobLayoutTypePage ? TO_DPIY(NATRON_SETTINGS_VERTICAL_SPACING_PIXELS) : 0;
        labelLayout->setContentsMargins(TO_DPIX(3), 0, 0, verticalMargin);
        labelLayout->setSpacing( TO_DPIY(2) );
    }

    KnobGuiPtr thisShared = _publicInterface->shared_from_this();

#pragma message WARN("Pass ViewSetSpec::all() instead of ViewIdx(0) here")
    descriptionLabel = new KnobClickableLabel(QString(), thisShared, ViewIdx(0), parentWidget);
    KnobGuiContainerHelper::setLabelFromTextAndIcon(descriptionLabel, QString::fromUtf8(labelText.c_str()), QString::fromUtf8(labelIconFilePath.c_str()), firstViewWidgets->isLabelBold());

    // Make a warning indicator
    warningIndicator = new Label(parentWidget);
    warningIndicator->setVisible(false);


    QFontMetrics fm(descriptionLabel->font(), 0);
    int pixSize = fm.height();
    QPixmap stdErrorPix;
    stdErrorPix = KnobGuiContainerHelper::getStandardIcon(QMessageBox::Critical, pixSize, descriptionLabel);
    warningIndicator->setPixmap(stdErrorPix);


    QObject::connect( descriptionLabel, SIGNAL(clicked(bool)), _publicInterface, SIGNAL(labelClicked(bool)) );


    // if multi-view, create an arrow to show/hide all dimensions
    if (views.size() > 1) {
        viewUnfoldArrow = new GroupBoxLabel(parentWidget);
        viewUnfoldArrow->setFixedSize(NATRON_MEDIUM_BUTTON_SIZE, NATRON_MEDIUM_BUTTON_SIZE);
        viewUnfoldArrow->setChecked(true);
        QObject::connect( viewUnfoldArrow, SIGNAL(checked(bool)), _publicInterface, SLOT(onMultiViewUnfoldClicked(bool)) );
    }


    if (isOnNewLine) {
        labelLayout->addWidget(warningIndicator);
        labelLayout->addWidget(descriptionLabel);
        if (viewUnfoldArrow) {
            labelLayout->addWidget(viewUnfoldArrow);
        }
    }

} // createLabel


void
KnobGui::createGUI(QWidget* parentWidget)
{
    _imp->guiRemoved = false;
    KnobIPtr knob = getKnob();

    // Set the isOnNewLineFlag for page layout
    if (_imp->layoutType == eKnobLayoutTypePage) {
        _imp->refreshIsOnNewLineFlag();
    }

    // Parmetric knobs use the customInteract to actually draw something on top of the background

    // Create row widgets for each view
    std::list<ViewIdx> views = knob->getViewsList();
    for (std::list<ViewIdx>::iterator it = views.begin(); it != views.end(); ++it) {
        createViewWidgets(parentWidget, *it);
    }


    assert(!_imp->views.empty());
    KnobGuiWidgetsPtr firstViewWidgets = _imp->views.begin()->second.widgets;

    // Create the label if needed
    if (firstViewWidgets->mustCreateLabelWidget()) {
        _imp->createLabel(parentWidget);
    }

    
    // If not on a new line, add to the previous knob layout the current knob
    if (!_imp->isOnNewLine) {
        QHBoxLayout* prevKnobLayout = _imp->views.begin()->second.fieldLayout;

        int spacing;
        bool isViewerParam = _imp->container->isInViewerUIKnob();
        if (isViewerParam) {
            spacing = _imp->container->getItemsSpacingOnSameLine();
        } else {
            spacing = _imp->spacingBeforePrevKnob;//knob->getSpacingBetweenitems();
            // Default sapcing is 0 on knobs, but use the default for the widget container so the UI doesn't appear cluttered
            // The minimum allowed spacing should be 1px
            if (spacing == 0) {
                spacing = _imp->container->getItemsSpacingOnSameLine();;
            }
        }
        if (spacing > 0) {
            prevKnobLayout->addSpacing( TO_DPIX(spacing) );
        }
        if (_imp->labelContainer) {
            prevKnobLayout->addWidget(_imp->labelContainer);
        } else {
            if (_imp->warningIndicator) {
                prevKnobLayout->addWidget(_imp->warningIndicator);
            }
            if (_imp->descriptionLabel) {
                prevKnobLayout->addWidget(_imp->descriptionLabel);
            }
        }
    }
    
    
    if (_imp->descriptionLabel) {
        toolTip(_imp->descriptionLabel, ViewIdx(0));
    }


    _imp->widgetCreated = true;


    // Refresh modifications state
    onHasModificationsChanged();

    
    // Refresh animation and expression state on all views
    for (int i = 0; i < knob->getNDimensions(); ++i) {
        for (KnobGuiPrivate::PerViewWidgetsMap::const_iterator it = _imp->views.begin(); it!=_imp->views.end(); ++it) {
            onExprChanged(DimIdx(i), it->first);
        }
    }

    // Refresh secretness
    setSecret();

} // KnobGui::createGUI

void
KnobGui::updateGuiInternal(DimSpec dimension, ViewSetSpec view)
{
    if (!_imp->customInteract) {
        if (view.isAll()) {
            for (KnobGuiPrivate::PerViewWidgetsMap::const_iterator it = _imp->views.begin(); it != _imp->views.end(); ++it) {
                it->second.widgets->updateGUI(dimension);
            }

        } else {
            ViewIdx view_i = getKnob()->getViewIdxFromGetSpec(ViewGetSpec(view));
            KnobGuiPrivate::PerViewWidgetsMap::const_iterator foundView = _imp->views.find(view_i);
            if (foundView != _imp->views.end()) {
                foundView->second.widgets->updateGUI(dimension);
            }
        }
    } else {
        _imp->customInteract->update();
    }
}

void
KnobGui::onRightClickClicked(const QPoint & pos)
{
    QWidget *widget = qobject_cast<QWidget *>( sender() );

    if (widget) {
        ViewIdx view(widget->property(KNOB_RIGHT_CLICK_VIEW_PROPERTY).toInt());
        DimSpec dim(widget->property(KNOB_RIGHT_CLICK_DIM_PROPERTY).toInt());
        showRightClickMenuForDimension(pos, dim, view);
    }
}

void
KnobGui::showRightClickMenuForDimension(const QPoint &,
                                        DimSpec dimension, ViewIdx view)
{
    KnobIPtr knob = getKnob();
    bool isViewerKnob = _imp->layoutType == KnobGui::eKnobLayoutTypeViewerUI;
    if ( (!isViewerKnob && knob->getIsSecret()) || (isViewerKnob && knob->getInViewerContextSecret()) ) {
        return;
    }

    createAnimationMenu(_imp->copyRightClickMenu, dimension, view);
    if (!_imp->views.empty()) {
        KnobGuiWidgetsPtr widgets = _imp->views.begin()->second.widgets;
        if (widgets) {
            widgets->addRightClickMenuEntries(_imp->copyRightClickMenu);
        }
    }
    _imp->copyRightClickMenu->exec( QCursor::pos() );
} // showRightClickMenuForDimension

Menu*
KnobGui::createInterpolationMenu(QMenu* menu,
                                 DimSpec dimension, ViewIdx view,
                                 bool isEnabled)
{
    Menu* interpolationMenu = new Menu(menu);
    QString title;

    if (dimension.isAll()) {
        title = tr("Interpolation (all dimensions)");
    } else {
        title = tr("Interpolation");
    }
    interpolationMenu->setTitle(title);
    if (!isEnabled) {
        interpolationMenu->menuAction()->setEnabled(false);
    }

    QList<QVariant> actionData;
    actionData.push_back((int)dimension);
    actionData.push_back((int)view);

    {
        QAction* constantInterpAction = new QAction(tr("Constant"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeConstant);
        constantInterpAction->setData( QVariant(actionData) );
        QObject::connect( constantInterpAction, SIGNAL(triggered()), this, SLOT(onConstantInterpActionTriggered()) );
        interpolationMenu->addAction(constantInterpAction);
    }

    {
        QAction* linearInterpAction = new QAction(tr("Linear"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeLinear);
        linearInterpAction->setData( QVariant(actionData) );
        QObject::connect( linearInterpAction, SIGNAL(triggered()), this, SLOT(onLinearInterpActionTriggered()) );
        interpolationMenu->addAction(linearInterpAction);
    }
    {
        QAction* smoothInterpAction = new QAction(tr("Smooth"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeSmooth);
        smoothInterpAction->setData( QVariant(actionData) );
        QObject::connect( smoothInterpAction, SIGNAL(triggered()), this, SLOT(onSmoothInterpActionTriggered()) );
        interpolationMenu->addAction(smoothInterpAction);
    }
    {
        QAction* catmullRomInterpAction = new QAction(tr("Catmull-Rom"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeCatmullRom);
        catmullRomInterpAction->setData( QVariant(actionData) );
        QObject::connect( catmullRomInterpAction, SIGNAL(triggered()), this, SLOT(onCatmullromInterpActionTriggered()) );
        interpolationMenu->addAction(catmullRomInterpAction);
    }
    {
        QAction* cubicInterpAction = new QAction(tr("Cubic"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeCubic);
        cubicInterpAction->setData( QVariant(actionData) );
        QObject::connect( cubicInterpAction, SIGNAL(triggered()), this, SLOT(onCubicInterpActionTriggered()) );
        interpolationMenu->addAction(cubicInterpAction);
    }
    {
        QAction* horizInterpAction = new QAction(tr("Horizontal"), interpolationMenu);
        actionData.push_back((int)eKeyframeTypeHorizontal);
        horizInterpAction->setData( QVariant(actionData) );
        QObject::connect( horizInterpAction, SIGNAL(triggered()), this, SLOT(onHorizontalInterpActionTriggered()) );
        interpolationMenu->addAction(horizInterpAction);
    }
    menu->addAction( interpolationMenu->menuAction() );
    
    return interpolationMenu;
}

void
KnobGui::getDimViewFromActionData(const QAction* action, ViewIdx* view, DimSpec* dimension)
{
    QList<QVariant> actionData = action->data().toList();
    if (actionData.size() != 2) {
        return;
    }
    QList<QVariant>::iterator it = actionData.begin();
    *dimension = DimSpec(it->toInt());
    ++it;
    *view = ViewIdx(it->toInt());
}

bool
KnobGui::getAllDimensionsVisible(ViewIdx view) const
{
    KnobGuiPrivate::PerViewWidgetsMap::const_iterator foundView = _imp->views.find(view);
    if (foundView == _imp->views.end()) {
        return false;
    }
    return foundView->second.widgets ? foundView->second.widgets->getAllDimensionsVisible() : false;
}

void
KnobGui::createAnimationMenu(QMenu* menu, DimSpec dimension, ViewIdx view)
{
    if ( (dimension == 0) && !getAllDimensionsVisible(view) ) {
        dimension = DimSpec::all();
    }

    KnobIPtr knob = getKnob();
    assert( dimension >= -1 && dimension < knob->getNDimensions() );
    menu->clear();
    bool dimensionHasKeyframeAtTime = false;
    bool hasAllKeyframesAtTime = true;
    int nDims = knob->getNDimensions();


    if (dimension.isAll() && nDims == 1) {
        dimension = DimSpec(0);
    }


    for (int i = 0; i < nDims; ++i) {
        AnimationLevelEnum lvl = knob->getAnimationLevel(DimIdx(i), view);
        if (lvl != eAnimationLevelOnKeyframe) {
            hasAllKeyframesAtTime = false;
        } else if ( dimension == i && (lvl == eAnimationLevelOnKeyframe) ) {
            dimensionHasKeyframeAtTime = true;
        }
    }

    bool hasDimensionSlaved = false;
    bool hasAnimation = false;
    bool dimensionHasAnimation = false;
    bool isDimensionEnabled = true;
    bool dimensionIsSlaved = false;
    bool hasDimensionDisabled = false;
    for (int i = 0; i < nDims; ++i) {
        if ( knob->isSlave(DimIdx(i), view) ) {
            hasDimensionSlaved = true;

            if (i == dimension) {
                dimensionIsSlaved = true;
            }
        }
        if (knob->getKeyFramesCount(view, DimIdx(i)) > 0) {
            hasAnimation = true;
            if (dimension == i) {
                dimensionHasAnimation = true;
            }
        }

        if (hasDimensionSlaved && hasAnimation) {
            break;
        }
        if ( !knob->isEnabled(DimIdx(i), view) ) {
            hasDimensionDisabled = true;
            if (dimension == DimIdx(i)) {
                isDimensionEnabled = false;
            }
        }
    }



    bool isAppKnob = knob->getHolder() && knob->getHolder()->getApp();

    if ( (knob->getNDimensions() > 1) && knob->isAnimationEnabled() && !hasDimensionSlaved && isAppKnob ) {
        ///Multi-dim actions
        if (!hasAllKeyframesAtTime) {
            QAction* setKeyAction = new QAction(tr("Set Key") + QLatin1Char(' ') + tr("(all dimensions)"), menu);
            QList<QVariant> actionData;
            actionData.push_back((int)DimSpec::all());
            actionData.push_back((int)view);
            setKeyAction->setData(actionData);
            QObject::connect( setKeyAction, SIGNAL(triggered()), this, SLOT(onSetKeyActionTriggered()) );
            menu->addAction(setKeyAction);
            if (hasDimensionDisabled) {
                setKeyAction->setEnabled(false);
            }
        } else {
            QAction* removeKeyAction = new QAction(tr("Remove Key") + QLatin1Char(' ') + tr("(all dimensions)"), menu);
            QList<QVariant> actionData;
            actionData.push_back((int)DimSpec::all());
            actionData.push_back((int)view);
            removeKeyAction->setData(actionData);
            QObject::connect( removeKeyAction, SIGNAL(triggered()), this, SLOT(onRemoveKeyActionTriggered()) );
            menu->addAction(removeKeyAction);
            if (hasDimensionDisabled) {
                removeKeyAction->setEnabled(false);
            }
        }

        if (hasAnimation) {
            QAction* removeAnyAnimationAction = new QAction(tr("Remove animation") + QLatin1Char(' ') + tr("(all dimensions)"), menu);
            QList<QVariant> actionData;
            actionData.push_back((int)DimSpec::all());
            actionData.push_back((int)view);
            removeAnyAnimationAction->setData(actionData);
            QObject::connect( removeAnyAnimationAction, SIGNAL(triggered()), this, SLOT(onRemoveAnimationActionTriggered()) );
            if (hasDimensionDisabled) {
                removeAnyAnimationAction->setEnabled(false);
            }
            menu->addAction(removeAnyAnimationAction);
        }
    }
    if ( ( (dimension != -1) || (knob->getNDimensions() == 1) ) && knob->isAnimationEnabled() && !dimensionIsSlaved && isAppKnob ) {
        if ( !menu->isEmpty() ) {
            menu->addSeparator();
        }
        {
            ///Single dim action
            if (!dimensionHasKeyframeAtTime) {
                QAction* setKeyAction = new QAction(tr("Set Key"), menu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                setKeyAction->setData(actionData);
                QObject::connect( setKeyAction, SIGNAL(triggered()), this, SLOT(onSetKeyActionTriggered()) );
                menu->addAction(setKeyAction);
                if (!isDimensionEnabled) {
                    setKeyAction->setEnabled(false);
                }
            } else {
                QAction* removeKeyAction = new QAction(tr("Remove Key"), menu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                removeKeyAction->setData(actionData);
                QObject::connect( removeKeyAction, SIGNAL(triggered()), this, SLOT(onRemoveKeyActionTriggered()) );
                menu->addAction(removeKeyAction);
                if (!isDimensionEnabled) {
                    removeKeyAction->setEnabled(false);
                }
            }

            if (dimensionHasAnimation) {
                QAction* removeAnyAnimationAction = new QAction(tr("Remove animation"), menu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                removeAnyAnimationAction->setData(actionData);
                QObject::connect( removeAnyAnimationAction, SIGNAL(triggered()), this, SLOT(onRemoveAnimationActionTriggered()) );
                menu->addAction(removeAnyAnimationAction);
                if (!isDimensionEnabled) {
                    removeAnyAnimationAction->setEnabled(false);
                }
            }
        }
    }
    if ( !menu->isEmpty() ) {
        menu->addSeparator();
    }

    if (hasAnimation && isAppKnob) {
        QAction* showInCurveEditorAction = new QAction(tr("Show in Animation Editor"), menu);
        QObject::connect( showInCurveEditorAction, SIGNAL(triggered()), this, SLOT(onShowInCurveEditorActionTriggered()) );
        menu->addAction(showInCurveEditorAction);
        if (hasDimensionDisabled) {
            showInCurveEditorAction->setEnabled(false);
        }

        if ( (knob->getNDimensions() > 1) && !hasDimensionSlaved ) {
            Menu* interpMenu = createInterpolationMenu(menu, DimSpec::all(), view, !hasDimensionDisabled);
            Q_UNUSED(interpMenu);
        }
        if (dimensionHasAnimation && !dimensionIsSlaved) {
            if ( !dimension.isAll() || (knob->getNDimensions() == 1) ) {
                Menu* interpMenu = createInterpolationMenu(menu, !dimension.isAll() ? dimension : DimSpec(0), view, isDimensionEnabled);
                Q_UNUSED(interpMenu);
            }
        }
    }


    {
        Menu* copyMenu = new Menu(menu);
        copyMenu->setTitle( tr("Copy") );

        if (!dimension.isAll() || nDims == 1) {
            if (hasAnimation && isAppKnob) {
                QAction* copyAnimationAction = new QAction(tr("Copy Animation"), copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                copyAnimationAction->setData(actionData);
                QObject::connect( copyAnimationAction, SIGNAL(triggered()), this, SLOT(onCopyAnimationActionTriggered()) );
                copyMenu->addAction(copyAnimationAction);
            }


            {
                QAction* copyValuesAction = new QAction(tr("Copy Value"), copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                copyValuesAction->setData(actionData);
                copyMenu->addAction(copyValuesAction);
                QObject::connect( copyValuesAction, SIGNAL(triggered()), this, SLOT(onCopyValuesActionTriggered()) );
            }

            if (isAppKnob) {
                QAction* copyLinkAction = new QAction(tr("Copy Link"), copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)dimension);
                actionData.push_back((int)view);
                copyLinkAction->setData(actionData);
                copyMenu->addAction(copyLinkAction);
                QObject::connect( copyLinkAction, SIGNAL(triggered()), this, SLOT(onCopyLinksActionTriggered()) );
            }
        }

        if (knob->getNDimensions() > 1) {
            if (hasAnimation && isAppKnob) {
                QString title = tr("Copy Animation");
                title += QLatin1Char(' ');
                title += tr("(all dimensions)");
                QAction* copyAnimationAction = new QAction(title, copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)DimSpec::all());
                actionData.push_back((int)view);
                copyAnimationAction->setData(actionData);
                QObject::connect( copyAnimationAction, SIGNAL(triggered()), this, SLOT(onCopyAnimationActionTriggered()) );
                copyMenu->addAction(copyAnimationAction);
            }
            {
                QString title = tr("Copy Values");
                title += QLatin1Char(' ');
                title += tr("(all dimensions)");
                QAction* copyValuesAction = new QAction(title, copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)DimSpec::all());
                actionData.push_back((int)view);
                copyValuesAction->setData(actionData);
                copyMenu->addAction(copyValuesAction);
                QObject::connect( copyValuesAction, SIGNAL(triggered()), this, SLOT(onCopyValuesActionTriggered()) );
            }

            if (isAppKnob) {
                QString title = tr("Copy Link");
                title += QLatin1Char(' ');
                title += tr("(all dimensions)");
                QAction* copyLinkAction = new QAction(title, copyMenu);
                QList<QVariant> actionData;
                actionData.push_back((int)DimSpec::all());
                actionData.push_back((int)view);
                copyLinkAction->setData(actionData);
                copyMenu->addAction(copyLinkAction);
                QObject::connect( copyLinkAction, SIGNAL(triggered()), this, SLOT(onCopyLinksActionTriggered()) );
            }

        }

        menu->addAction( copyMenu->menuAction() );
    }

    ///If the clipboard is either empty or has no animation, disable the Paste animation action.
    KnobIPtr fromKnob;
    KnobClipBoardType type;

    //cbDim is ignored for now
    DimSpec cbDim;
    ViewIdx cbView;
    appPTR->getKnobClipBoard(&type, &fromKnob, &cbDim, &cbView);


    if (fromKnob && (fromKnob != knob) && isAppKnob) {
        if ( fromKnob->typeName() == knob->typeName() ) {
            QString titlebase;
            if (type == eKnobClipBoardTypeCopyValue) {
                titlebase = tr("Paste Value");
            } else if (type == eKnobClipBoardTypeCopyAnim) {
                titlebase = tr("Paste Animation");
            } else if (type == eKnobClipBoardTypeCopyLink) {
                titlebase = tr("Paste Link");
            }

            bool ignorePaste = (!knob->isAnimationEnabled() && type == eKnobClipBoardTypeCopyAnim) ||
                               ( (dimension.isAll() || cbDim.isAll()) && knob->getNDimensions() != fromKnob->getNDimensions() );
            if (!ignorePaste) {
                if ( cbDim.isAll() && ( fromKnob->getNDimensions() == knob->getNDimensions() ) && !hasDimensionSlaved ) {
                    QString title = titlebase;
                    if (knob->getNDimensions() > 1) {
                        title += QLatin1Char(' ');
                        title += tr("(all dimensions)");
                    }
                    QAction* pasteAction = new QAction(title, menu);
                    QList<QVariant> actionData;
                    actionData.push_back((int)DimSpec::all());
                    actionData.push_back((int)view);
                    pasteAction->setData(actionData);
                    QObject::connect( pasteAction, SIGNAL(triggered()), this, SLOT(onPasteActionTriggered()) );
                    menu->addAction(pasteAction);
                    if (hasDimensionDisabled) {
                        pasteAction->setEnabled(false);
                    }
                }

                if ( ( !dimension.isAll() || (knob->getNDimensions() == 1) ) && !dimensionIsSlaved ) {
                    QAction* pasteAction = new QAction(titlebase, menu);
                    QList<QVariant> actionData;
                    actionData.push_back(dimension.isAll() ? 0 : (int)dimension);
                    actionData.push_back((int)view);
                    pasteAction->setData(actionData);
                    QObject::connect( pasteAction, SIGNAL(triggered()), this, SLOT(onPasteActionTriggered()) );
                    menu->addAction(pasteAction);
                    if ((!dimension.isAll() && !isDimensionEnabled) || (dimension.isAll() && hasDimensionDisabled)) {
                        pasteAction->setEnabled(false);
                    }
                }
            }
        }
    }

    if ( (knob->getNDimensions() > 1) && !hasDimensionSlaved ) {
        QAction* resetDefaultAction = new QAction(tr("Reset to default") + QLatin1Char(' ') + tr("(all dimensions)"), _imp->copyRightClickMenu);
        QList<QVariant> actionData;
        actionData.push_back((int)DimSpec::all());
        actionData.push_back((int)view);
        resetDefaultAction->setData(actionData);
        QObject::connect( resetDefaultAction, SIGNAL(triggered()), this, SLOT(onResetDefaultValuesActionTriggered()) );
        menu->addAction(resetDefaultAction);
        if (hasDimensionDisabled) {
            resetDefaultAction->setEnabled(false);
        }
    }
    if ( ( (dimension != -1) || (knob->getNDimensions() == 1) ) && !dimensionIsSlaved ) {
        QAction* resetDefaultAction = new QAction(tr("Reset to default"), _imp->copyRightClickMenu);
        QList<QVariant> actionData;
        actionData.push_back((int)dimension);
        actionData.push_back((int)view);
        resetDefaultAction->setData(actionData);
        QObject::connect( resetDefaultAction, SIGNAL(triggered()), this, SLOT(onResetDefaultValuesActionTriggered()) );
        menu->addAction(resetDefaultAction);
        if (!isDimensionEnabled) {
            resetDefaultAction->setEnabled(false);
        }
    }

    if ( !menu->isEmpty() ) {
        menu->addSeparator();
    }


    bool dimensionHasExpression = false;
    bool hasExpression = false;
    for (int i = 0; i < knob->getNDimensions(); ++i) {
        std::string dimExpr = knob->getExpression(DimIdx(i), view);
        if (i == dimension) {
            dimensionHasExpression = !dimExpr.empty();
        }
        hasExpression |= !dimExpr.empty();
    }
    if ( (knob->getNDimensions() > 1) && !hasDimensionSlaved && isAppKnob ) {
        {
            QAction* setExprsAction = new QAction( ( hasExpression ? tr("Edit expression") :
                                                    tr("Set expression") ) + QLatin1Char(' ') + tr("(all dimensions)"), menu );
            QList<QVariant> actionData;
            actionData.push_back((int)DimSpec::all());
            actionData.push_back((int)view);
            setExprsAction->setData(actionData);
            QObject::connect( setExprsAction, SIGNAL(triggered()), this, SLOT(onSetExprActionTriggered()) );
            if (hasDimensionDisabled) {
                setExprsAction->setEnabled(false);
            }
            menu->addAction(setExprsAction);
        }
        if (hasExpression) {
            QAction* clearExprAction = new QAction(tr("Clear expression") + QLatin1Char(' ') + tr("(all dimensions)"), menu);
            QObject::connect( clearExprAction, SIGNAL(triggered()), this, SLOT(onClearExprActionTriggered()) );
            QList<QVariant> actionData;
            actionData.push_back((int)DimSpec::all());
            actionData.push_back((int)view);
            clearExprAction->setData(actionData);
            if (!isDimensionEnabled) {
                clearExprAction->setEnabled(false);
            }
            menu->addAction(clearExprAction);
        }
    }
    if ( ( (dimension != -1) || (knob->getNDimensions() == 1) ) && !dimensionIsSlaved && isAppKnob ) {
        {
            QAction* setExprAction = new QAction(dimensionHasExpression ? tr("Edit expression...") : tr("Set expression..."), menu);
            QObject::connect( setExprAction, SIGNAL(triggered()), this, SLOT(onSetExprActionTriggered()) );
            QList<QVariant> actionData;
            actionData.push_back((int)dimension);
            actionData.push_back((int)view);
            setExprAction->setData(actionData);
            if (!isDimensionEnabled) {
                setExprAction->setEnabled(false);
            }
            menu->addAction(setExprAction);
        }
        if (dimensionHasExpression) {
            QAction* clearExprAction = new QAction(tr("Clear expression"), menu);
            QObject::connect( clearExprAction, SIGNAL(triggered()), this, SLOT(onClearExprActionTriggered()) );
            QList<QVariant> actionData;
            actionData.push_back((int)dimension);
            actionData.push_back((int)view);
            clearExprAction->setData(actionData);
            if (!isDimensionEnabled) {
                clearExprAction->setEnabled(false);
            }
            menu->addAction(clearExprAction);
        }
    }


    ///find-out to which node that master knob belongs to
    KnobHolderPtr holder = knob->getHolder();
    EffectInstancePtr isEffect = toEffectInstance(holder);
    NodeCollectionPtr collec;
    NodeGroupPtr isCollecGroup;
    if (isEffect) {
        collec = isEffect->getNode()->getGroup();
        isCollecGroup = toNodeGroup(collec);
    }


    if ( isAppKnob && ( ( hasDimensionSlaved && dimension.isAll() ) || dimensionIsSlaved ) ) {
        menu->addSeparator();

        KnobIPtr aliasMaster = knob->getAliasMaster();
        std::string knobName;

        if ( aliasMaster || ((!dimension.isAll()|| knob->getNDimensions() == 1) && dimensionIsSlaved) ) {

            KnobIPtr masterKnob;
            MasterKnobLink linkData;
            if (aliasMaster) {
                masterKnob = aliasMaster;
            } else {
                (void)knob->getMaster(DimIdx(dimension), view, &linkData);
                masterKnob = linkData.masterKnob.lock();
            }

            KnobHolderPtr masterHolder = masterKnob->getHolder();
            if (masterHolder) {
                KnobTableItemPtr isTableItem = toKnobTableItem(masterHolder);
                EffectInstancePtr isEffect = toEffectInstance(masterHolder);
                if (isTableItem) {
                    knobName.append( isTableItem->getModel()->getNode()->getScriptName() );
                    knobName += '.';
                    knobName += isTableItem->getFullyQualifiedName();
                } else if (isEffect) {
                    knobName += isEffect->getScriptName_mt_safe();
                }

            }


            knobName.append(".");
            knobName.append( masterKnob->getName() );
            if ( !aliasMaster && (masterKnob->getNDimensions() > 1) ) {
                knobName.append(".");
                knobName.append( masterKnob->getDimensionName(linkData.masterDimension) );
            }
        }
        QString actionText;
        if (aliasMaster) {
            actionText.append( tr("Remove Alias link") );
        } else {
            actionText.append( tr("Unlink") );
        }
        if ( !knobName.empty() ) {
            actionText.append( QString::fromUtf8(" from ") );
            actionText.append( QString::fromUtf8( knobName.c_str() ) );
        }
        QAction* unlinkAction = new QAction(actionText, menu);
        unlinkAction->setData( QVariant(dimension) );
        QObject::connect( unlinkAction, SIGNAL(triggered()), this, SLOT(onUnlinkActionTriggered()) );
        menu->addAction(unlinkAction);
    }
    KnobI::ListenerDimsMap listeners;
    knob->getListeners(listeners);
    if ( !listeners.empty() ) {
        KnobIPtr listener = listeners.begin()->first.lock();
        if ( listener && (listener->getAliasMaster() == knob) ) {
            QAction* removeAliasLink = new QAction(tr("Remove alias link"), menu);
            QObject::connect( removeAliasLink, SIGNAL(triggered()), this, SLOT(onRemoveAliasLinkActionTriggered()) );
            menu->addAction(removeAliasLink);
        }
    }
    if ( isCollecGroup && !knob->getAliasMaster() ) {
        QAction* createMasterOnGroup = new QAction(tr("Create alias on group"), menu);
        QObject::connect( createMasterOnGroup, SIGNAL(triggered()), this, SLOT(onCreateAliasOnGroupActionTriggered()) );
        menu->addAction(createMasterOnGroup);
    }
} // createAnimationMenu

KnobIPtr
KnobGui::createDuplicateOnNode(const EffectInstancePtr& effect,
                               bool makeAlias,
                               const KnobPagePtr& page,
                               const KnobGroupPtr& group,
                               int indexInParent)
{
    ///find-out to which node that master knob belongs to
    assert( getKnob()->getHolder()->getApp() );
    KnobIPtr knob = getKnob();

    if (!makeAlias) {
        std::list<ViewIdx> views = knob->getViewsList();
        for (int i = 0; i < knob->getNDimensions(); ++i) {
            for (std::list<ViewIdx>::const_iterator it = views.begin(); it!=views.end(); ++it) {
                std::string expr = knob->getExpression(DimIdx(i), *it);
                if ( !expr.empty() ) {
                    StandardButtonEnum rep = Dialogs::questionDialog( tr("Expression").toStdString(), tr("This operation will create "
                                                                                                         "an expression link between this parameter and the new parameter on the group"
                                                                                                         " which will wipe the current expression(s).\n"
                                                                                                         "Continue anyway?").toStdString(), false,
                                                                     StandardButtons(eStandardButtonOk | eStandardButtonCancel) );
                    if (rep != eStandardButtonYes) {
                        return KnobIPtr();
                    }
                }
            }

        }
    }

    EffectInstancePtr isEffect = toEffectInstance( knob->getHolder() );
    if (!isEffect) {
        return KnobIPtr();
    }
    const std::string& nodeScriptName = isEffect->getNode()->getScriptName();
    std::string newKnobName = nodeScriptName +  knob->getName();
    KnobIPtr ret;
    try {
        ret = knob->createDuplicateOnHolder(effect,
                                            page,
                                            group,
                                            indexInParent,
                                            makeAlias,
                                            newKnobName,
                                            knob->getLabel(),
                                            knob->getHintToolTip(),
                                            true,
                                            true);
    } catch (const std::exception& e) {
        Dialogs::errorDialog( tr("Error while creating parameter").toStdString(), e.what() );

        return KnobIPtr();
    }

    if (ret) {
        NodeGuiIPtr groupNodeGuiI = effect->getNode()->getNodeGui();
        NodeGuiPtr groupNodeGui = toNodeGui( groupNodeGuiI );
        assert(groupNodeGui);
        if (groupNodeGui) {
            groupNodeGui->ensurePanelCreated();
        }
    }
    effect->getApp()->triggerAutoSave();

    return ret;
} // KnobGui::createDuplicateOnNode

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_KnobGui.cpp"
