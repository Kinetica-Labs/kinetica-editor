#pragma once
// The editor language is selected before the window is built. Only presentation text goes through
// uiText; scene data and operation identifiers belong to their original contracts.
#include <QString>
#include <QTranslator>

class QCoreApplication;

class UiLanguage {
public:
    explicit UiLanguage(QCoreApplication& app, const QString& code);
    static QString preference();
    static bool setPreference(const QString& code);
    static QString active();

private:
    QTranslator translation_;
};

// UTF-8 for Qt widgets and existing label tables that use const char*. The returned text has
// application lifetime. Qt Linguist recognizes this function as an alias for translate().
const char* uiText(const char* context, const char* source);

enum class UiCount { Object, Array, Instance, InstanceOf, Copy };
QString uiCount(int count, UiCount noun);
