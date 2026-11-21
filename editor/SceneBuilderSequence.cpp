// The Sequencer's preview in edit mode (SceneBuilder.h): the active sequence's poses at the playhead drawn while it is
// dragged or played, the graph put back when it ends - the panel (SequencerPanel) then makes the one edit through the
// API. The sequences themselves, their keys and the playhead as an edit are the core's (scene/Animation.h, the
// sequence_* operations).
#include "SceneBuilder.h"

#include "scene/Animation.h"

void SceneBuilder::beginSequencePreview() {
    if (editing() && !previewFrom_) previewFrom_ = graph_;
}

void SceneBuilder::previewSequence(double time) {
    ke::SequenceObject* q = ke::findSequence(graph_, 0);
    if (!editing() || !previewFrom_ || !q) return;
    ke::seekSequence(graph_, *q, ke::Real(time));
    refreshView();
    refreshLightsAndCameras();
    if (const ke::SceneObject* o = selectedObject()) showTransform(*o);
}

void SceneBuilder::endSequencePreview() {
    if (!previewFrom_) return;
    graph_ = std::move(*previewFrom_);
    previewFrom_.reset();
}
