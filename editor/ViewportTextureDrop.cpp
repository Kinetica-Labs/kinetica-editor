// Dropping a picture file on an object in edit mode: the object under the pointer lights up while the
// file is dragged over it, and the drop gives it that picture as its texture (imageDropped).
#include "Viewport.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QImageReader>

void Viewport::dragEnterEvent(QDragEnterEvent* e) {
    draggedImage_.clear();
    const auto urls = e->mimeData()->urls();
    if (!editMode_ || urls.size() != 1 || !urls.front().isLocalFile()) return;
    const QString file = urls.front().toLocalFile();
    QImageReader reader(file);
    if (!reader.canRead()) return;
    draggedImage_ = file; e->setDropAction(Qt::CopyAction); e->accept();
}
void Viewport::dragMoveEvent(QDragMoveEvent* e) {
    if (!editMode_ || draggedImage_.isEmpty()) return;
    const uint32_t id = pickEntity(camera_.screenRay(e->position(), size()));
    setHoveredEntity(id);
    if (id) { e->setDropAction(Qt::CopyAction); e->accept(); }
}
void Viewport::dragLeaveEvent(QDragLeaveEvent* e) {
    draggedImage_.clear(); setHoveredEntity(0); e->accept();
}
void Viewport::dropEvent(QDropEvent* e) {
    if (!editMode_ || draggedImage_.isEmpty()) return;
    const uint32_t id = pickEntity(camera_.screenRay(e->position(), size()));
    const QString file = draggedImage_;
    draggedImage_.clear(); setHoveredEntity(0);
    if (id) { emit imageDropped(id, file); e->setDropAction(Qt::CopyAction); e->accept(); }
}
