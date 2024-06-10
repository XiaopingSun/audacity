#include "selectioncontroller.h"

#include "log.h"

using namespace au::projectscene;
using namespace au::project;
using namespace au::processing;

SelectionController::SelectionController(QObject* parent)
    : QObject(parent)
{
}

IProjectViewStatePtr SelectionController::viewState() const
{
    IAudacityProjectPtr prj = globalContext()->currentProject();
    return prj ? prj->viewState() : nullptr;
}

std::vector<TrackId> SelectionController::trackIdList() const
{
    ProcessingProjectPtr prj = globalContext()->currentProcessingProject();
    return prj ? prj->trackIdList() : std::vector<TrackId>();
}

void SelectionController::onSelectedCoords(double x1, double y1, double x2, double y2)
{
    LOGDA() << "x1: " << x1 << " y1: " << y1 << " x2: " << x2 << " y2: " << y2;

    QList<int> tracks = determinateTracks(y1, y2);
    setSelectedTracks(tracks);
}

void SelectionController::resetSelection()
{
    setSelectedTracks(QList<int>());
}

QList<int> SelectionController::determinateTracks(double y1, double y2) const
{
    IProjectViewStatePtr vs = viewState();
    if (!vs) {
        return { -1, -1 };
    }

    if (y1 < 0 && y2 < 0) {
        return { -1, -1 };
    }

    if (y1 > y2) {
        std::swap(y1, y2);
    }

    if (y1 < 1) {
        y1 = 1;
    }

    std::vector<TrackId> tracks = trackIdList();
    if (tracks.empty()) {
        return { -1, -1 };
    }

    QList<int> ret;

    int tracksVericalY = vs->tracksVericalY().val;
    int trackTop = -tracksVericalY;
    int trackBottom = trackTop;

    for (TrackId trackId : tracks) {
        trackTop = trackBottom;
        trackBottom = trackTop + vs->trackHeight(trackId).val;

        if (y1 > trackTop && y1 < trackBottom) {
            ret.append(trackId);
        }

        if (y2 > trackTop && y2 < trackBottom) {
            if (ret.back() != trackId) {
                ret.append(trackId);
            }
            break;
        }

        if (!ret.empty() && ret.back() != trackId) {
            ret.append(trackId);
            continue;
        }
    }

    return ret;
}

TimelineContext* SelectionController::timelineContext() const
{
    return m_context;
}

void SelectionController::setTimelineContext(TimelineContext* newContext)
{
    if (m_context == newContext) {
        return;
    }
    m_context = newContext;
    emit timelineContextChanged();
}

QList<int> SelectionController::selectedTracks() const
{
    return m_selectedTracks;
}

void SelectionController::setSelectedTracks(const QList<int>& tracks)
{
    if (m_selectedTracks == tracks) {
        return;
    }
    m_selectedTracks = tracks;
    emit selectedTracksChanged();
}
