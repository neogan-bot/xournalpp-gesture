/*
 * Xournal++
 *
 * Handle touchscreen zooming and panning.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <set>
#include <vector>

#include <gdk/gdk.h>  // for GdkEventSequence

#include "util/Point.h"  // for Point

#include "AbstractInputHandler.h"  // for AbstractInputHandler

class InputContext;
struct InputEvent;

class ActiveEvent {
    public:
    GdkEventSequence* sequence{};
    xoj::util::Point<double> lastPos;
    xoj::util::Point<double> distMoved{0,0};

    ActiveEvent(InputEvent const& event);

    void moved(InputEvent const& event);
};

class TouchInputHandler: public AbstractInputHandler {
private:
    bool zooming = false;
    bool moving = false;
    
    std::set<GdkEventSequence*> invalidActive;
    std::vector<ActiveEvent> validActive;

    double startZoomDistance = 0.0;
    xoj::util::Point<double> lastZoomScrollCenter{};

    xoj::util::Point<double> priLastAbs{-1.0, -1.0};
    xoj::util::Point<double> secLastAbs{-1.0, -1.0};

    xoj::util::Point<double> priLastRel{-1.0, -1.0};
    xoj::util::Point<double> secLastRel{-1.0, -1.0};

    // True, if a zoom sequence may be started by a motion event.
    bool startZoomReady{false};

    bool canBlockZoom{false};

private:
    void sequenceStart(InputEvent const& event);
    void scrollMotion(InputEvent const& event);
    void zoomStart();
    void zoomMotion(InputEvent const& event);
    void zoomEnd();

    void invalidateAllValid();
    std::vector<ActiveEvent>::iterator findEvent(GdkEventSequence* sequence);
    bool removeValidEvent(GdkEventSequence* event);
    bool tapGestureValid();
    const xoj::util::Point<double> center{0,0};
public:
    explicit TouchInputHandler(InputContext* inputContext);
    ~TouchInputHandler() override = default;

    bool handleImpl(InputEvent const& event) override;
    void onBlock() override;
    void onUnblock() override;
};
