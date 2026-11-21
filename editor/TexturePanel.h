#pragma once
// The "Внешний вид" card: a shape's surface (ke::SurfaceTexture) - a pattern or a picture, its scale,
// its bump - independent of its physical components; a picture is chosen or dropped here (or on the
// object in the 3D view). The bump rows are TextureBumpPanel.cpp. SceneBuilder owns undo and saving.
#include "core/SurfaceTexture.h"
#include <QWidget>
#include <QImage>
class QCheckBox;
class QVBoxLayout;
class QFormLayout;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class TexturePanel : public QWidget {
    Q_OBJECT
public:
    explicit TexturePanel(QWidget* parent = nullptr);
    void setTexture(const ke::SurfaceTexture& value, const QString& directory, ke::Vector3 size);
    bool assignImage(const QString& file);
    void writeTo(ke::SurfaceTexture& value) const;
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
signals:
    void edited();
private:
    QComboBox* kind_;
    QDoubleSpinBox* repeats_;
    QLineEdit* file_;
    QCheckBox* bump_;
    QDoubleSpinBox* bumpHeight_;
    QLineEdit* heightFile_;
    QLineEdit* normalFile_; // a normal map: a picture of the surface's normals, in place of or over the bump
    void buildBumpControls(QVBoxLayout* col, QFormLayout* form);
    QLabel* status_;
    QLabel* preview_;
    QString previewKey_;
    QImage previewImage_;
    double fitRepeats_ = 8;
    QString directory_;
    bool filling_ = false;
    void refresh();
};
