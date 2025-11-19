//
// Created by ulrich on 08.04.19.
//

#include "TouchInputHandler.h"

#include <cmath>  // for abs

#include "control/Control.h"                        // for Control
#include "control/settings/Settings.h"              // for Settings
#include "control/zoom/ZoomControl.h"               // for ZoomControl
#include "gui/Layout.h"                             // for Layout
#include "gui/MainWindow.h"                         // for MainWindow
#include "gui/XournalView.h"                        // for XournalView
#include "gui/inputdevices/AbstractInputHandler.h"  // for AbstractInputHandler
#include "gui/inputdevices/InputEvents.h"           // for InputEvent, BUTTO...

#include "InputContext.h"  // for InputContext

TouchInputHandler::TouchInputHandler(InputContext* inputContext): AbstractInputHandler(inputContext) {}

auto TouchInputHandler::handleImpl(InputEvent const& event) -> bool {
    if (!event.sequence) {
        // On x11, a GDK_MOTION_EVENT with sequence == nullptr is emitted before TOUCH_BEGIN: Ignore it
        // In general, every valid touch event must have a sequence
        return false;
    }
    bool zoomGesturesEnabled = inputContext->getSettings()->isZoomGesturesEnabled();

    if (event.type == BUTTON_PRESS_EVENT) {
        if (invalidActive.find(event.sequence) != invalidActive.end()) {
            g_warning("Missed touch end/cancel event. Resetting touch input handler.");
            invalidActive.clear();
        }
        if (invalidActive.empty()) {
            // All touches are previously valid and we did not miss an end/cancel event for the current touch

            validActive.push_back(ActiveEvent(event));

            sequenceStart(event);
            if (validActive.size() == 2) {
                startZoomReady = true;
            }
        } else {
            invalidActive.insert(event.sequence);
            g_debug("Add touch as invalid. %zu touches are invalid now.", invalidActive.size());
        }
        return true;
    }

    if (event.type == MOTION_EVENT) {
        if(validActive.size() > 1) {
            auto ev = TouchInputHandler::findEvent(event.sequence);
            ev->moved(event);
        }
        
        switch (validActive.size()) {
            case 1:
                scrollMotion(event);
                return true;
            case 2:
                if (event.sequence && (validActive[0].sequence == event.sequence || validActive[1].sequence == event.sequence)) {
                    xoj_assert(validActive[0].sequence);
                    if (zoomGesturesEnabled) {
                        if (startZoomReady) {
                            if (validActive[0].sequence == event.sequence) {
                                sequenceStart(event);
                                zoomStart();
                            }
                        } else {
                            zoomMotion(event);
                        }
                    } else {
                        scrollMotion(event);
                    }
                }
                return true;
            default:
                return true;
        }
    }

    if (event.type == BUTTON_RELEASE_EVENT) {
        switch (validActive.size()) {
            case 1:
                break;
            case 2:
                // proceed only if the event belongs to the fingers that summoned it
                if (validActive[0].sequence == event.sequence || validActive[1].sequence == event.sequence) {
                    if(zooming) {
                        zoomEnd();
                    } 
                    else if (tapGestureValid()) {
                        // no motion events were sent, perform gesture
                        this->inputContext->getView()->getControl()->getUndoRedoHandler()->undo();

                        // invalidate all points, we don't want to come back to zoomin
                        invalidateAllValid();
                    } 
                }
                break;
            
            case 3:
                if(tapGestureValid()) {
                    // run action
                    this->inputContext->getView()->getControl()->getUndoRedoHandler()->redo();
                }
                // default behaviour invalidates all touches regardless when >2 fingers show up
                invalidateAllValid();
                break;
            case 4:
                if(tapGestureValid()) {
                    // run action, for example floating toolbox
                    this->inputContext->getView()->getControl()->showFloatingToolbox(event.absolute.x, event.absolute.y);
                }
                invalidateAllValid();
                break;
            default:
                invalidateAllValid();
                break;
        }

        if (!removeValidEvent(event.sequence)){
            invalidActive.erase(event.sequence);
            g_debug("Removing sequence from invalid list, %zu inputs remain invalid.", invalidActive.size());
        }
        return true;
    }

    return false;
}

void TouchInputHandler::invalidateAllValid() {
    for (auto value : validActive) {
        invalidActive.insert(value.sequence);
    }

    validActive.clear();
}

bool TouchInputHandler::removeValidEvent(GdkEventSequence* sequence) {
    for (auto iter = validActive.begin();iter<validActive.end();iter++) {
        if (sequence == iter->sequence) {
            validActive.erase(iter);
            return true;
        }
    }

    return false;
}

// check if fingers moved during the gestore
// true if they didn't move far enough (5px rn)
bool TouchInputHandler::tapGestureValid() {
    for (auto iter = validActive.begin();iter<validActive.end();iter++) {
        if (abs(center.distance(iter->distMoved)) > 5) {
            return false;
        }
    }
    return true;
}

// find ActiveEvent in ActiveEvents by the sequence
std::vector<ActiveEvent>::iterator TouchInputHandler::findEvent(GdkEventSequence* sequence) {
    // maybe use a different structure or maybe thre's an assumption I can make to get rid of a search?
    // buuuutt it's 5 el max...
    for (auto iter = validActive.begin();iter<validActive.end();iter++) {
        if (sequence == iter->sequence) {
            return iter;
        }
    }

    return validActive.end();
}

void TouchInputHandler::sequenceStart(InputEvent const& event) {
    if (validActive[0].sequence == event.sequence) {
        this->priLastAbs = event.absolute;
        this->priLastRel = event.relative;
    } else {
        this->secLastAbs = event.absolute;
        this->secLastRel = event.relative;
    }
}

void TouchInputHandler::scrollMotion(InputEvent const& event) {
    // Will only be called if there is a single sequence (zooming handles two sequences)
    auto offset = [&]() {
        if (event.sequence == validActive[0].sequence) {
            auto offset = event.absolute - this->priLastAbs;
            this->priLastAbs = event.absolute;
            return offset;
        } else {
            auto offset = event.absolute - this->secLastAbs;
            this->secLastAbs = event.absolute;
            return offset;
        }
    }();

    inputContext->getView()->getLayout()->scrollRelative(-offset.x, -offset.y);
}

void TouchInputHandler::zoomStart() {
    this->zooming = true;
    this->startZoomDistance = this->priLastAbs.distance(this->secLastAbs);

    if (this->startZoomDistance == 0.0) {
        this->startZoomDistance = 0.01;
    }

    // Whether we can ignore the zoom portion of the gesture (e.g. distance between touch points
    // hasn't changed enough).
    this->canBlockZoom = true;

    ZoomControl* zoomControl = this->inputContext->getView()->getControl()->getZoomControl();

    // Disable zoom fit as we are zooming currently
    // TODO(fabian): this should happen internally!!!
    if (zoomControl->isZoomFitMode()) {
        zoomControl->setZoomFitMode(false);
    }

    // use screen pixel coordinates for the zoom center
    // as relative coordinates depend on the changing zoom level
    auto center = (this->priLastAbs + this->secLastAbs) / 2.0;
    this->lastZoomScrollCenter = center;

    // translate absolute window coordinates to the widget-local coordinates
    const auto* mainWindow = inputContext->getView()->getControl()->getWindow();
    const auto translation = mainWindow->getNegativeXournalWidgetPos();
    center += translation;

    zoomControl->startZoomSequence(center);

    this->startZoomReady = false;
}

void TouchInputHandler::zoomMotion(InputEvent const& event) {
    if (event.sequence == validActive[0].sequence) {
        this->priLastAbs = event.absolute;
    } else {
        this->secLastAbs = event.absolute;
    }

    double distance = this->priLastAbs.distance(this->secLastAbs);
    double zoom = distance / this->startZoomDistance;

    double zoomTriggerThreshold = inputContext->getSettings()->getTouchZoomStartThreshold();
    double zoomChangePercentage = std::abs(distance - startZoomDistance) / startZoomDistance * 100;

    // Has the touch points moved far enough to trigger a zoom?
    if (this->canBlockZoom && zoomChangePercentage < zoomTriggerThreshold) {
        zoom = 1.0;
    } else {
        // Touches have moved far enough from their initial location that we
        // no longer prevent touchscreen zooming.
        this->canBlockZoom = false;
    }

    ZoomControl* zoomControl = this->inputContext->getView()->getControl()->getZoomControl();
    const auto center = (this->priLastAbs + this->secLastAbs) / 2;
    zoomControl->zoomSequenceChange(zoom, true, center - lastZoomScrollCenter);
    lastZoomScrollCenter = center;
}

void TouchInputHandler::zoomEnd() {
    this->zooming = false;
    ZoomControl* zoomControl = this->inputContext->getView()->getControl()->getZoomControl();
    zoomControl->endZoomSequence();
}

void TouchInputHandler::onBlock() {
    if (this->zooming) {
        zoomEnd();
    }
}

void TouchInputHandler::onUnblock() {
    this->validActive.clear();
    this->invalidActive.clear();

    this->startZoomDistance = 0.0;
    this->lastZoomScrollCenter = {};

    priLastAbs = {-1.0, -1.0};
    secLastAbs = {-1.0, -1.0};
    priLastRel = {-1.0, -1.0};
    secLastRel = {-1.0, -1.0};
}

ActiveEvent::ActiveEvent(InputEvent const& event) {
    sequence = event.sequence;
    lastPos = event.absolute;
}

void ActiveEvent::moved(InputEvent const& event){
    xoj_assert(sequence == event.sequence);
    distMoved = event.absolute - lastPos;
}