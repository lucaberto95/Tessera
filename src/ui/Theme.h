#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QMainWindow;
class QWidget;

// Visual identity of the application (palette, style sheet) and the custom window frame: the system
// title bar is removed and replaced by a bar drawn by the application (menu, title, window buttons on
// Windows/Linux; traffic lights on macOS).
namespace Ui {

// Fusion style, light palette and global style sheet. Call once after creating the QApplication.
void applyTheme(QApplication &app);

// Replaces the system frame of \a w (a QMainWindow or QDialog) with the custom one. Idempotent.
void frameWindow(QWidget *w);

// Frames every dialog and main window the application shows, when it is first shown.
void installAutoFrame(QApplication &app);

// Icon from the bundled Material Symbols font (ligature name, e.g. "undo"); a null icon when the font
// is not available (tests, missing resource).
QIcon materialIcon(const QString &name, const QColor &color, int size = 20);

} // namespace Ui
