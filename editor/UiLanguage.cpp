// Language selection and translation catalogs for the Qt editor. See UiLanguage.h.
#include "UiLanguage.h"
#include "RuPlural.h"

#include <QCoreApplication>
#include <QSettings>

#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace {
constexpr auto settingsKey = "ui/language";
bool useEnglish = false;
}

UiLanguage::UiLanguage(QCoreApplication& app, const QString& code) {
    if (code == "en" && translation_.load(":/i18n/kinetica_en.qm")) {
        app.installTranslator(&translation_);
        useEnglish = true;
        return;
    }
    if (code == "en") qWarning("The English translation could not be loaded.");
    if (translation_.load(":/i18n/qtbase_ru.qm")) app.installTranslator(&translation_);
}

QString UiLanguage::preference() {
    return QSettings().value(settingsKey).toString() == "en" ? "en" : "ru";
}

bool UiLanguage::setPreference(const QString& code) {
    QSettings settings;
    settings.setValue(settingsKey, code == "en" ? "en" : "ru");
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString UiLanguage::active() { return useEnglish ? "en" : "ru"; }

const char* uiText(const char* context, const char* source) {
    if (!useEnglish) return source;
    using Key = std::pair<std::string, std::string>;
    static std::mutex lock;
    static std::map<Key, QByteArray> cache;
    const std::lock_guard guard(lock);
    auto [item, inserted] = cache.try_emplace(Key(context, source));
    if (inserted) item->second = QCoreApplication::translate(context, source).toUtf8();
    return item->second.constData();
}

QString uiCount(int count, UiCount noun) {
    struct Forms { const char* source; const char* one; const char* few; const char* many; };
    static const Forms forms[] = {
        {QT_TRANSLATE_N_NOOP("Kinetica", "%n объект"), "объект", "объекта", "объектов"},
        {QT_TRANSLATE_N_NOOP("Kinetica", "%n массив"), "массив", "массива", "массивов"},
        {QT_TRANSLATE_N_NOOP("Kinetica", "%n экземпляр"), "экземпляр", "экземпляра", "экземпляров"},
        {QT_TRANSLATE_N_NOOP("Kinetica", "%n экземпляра"), "экземпляра", "экземпляров", "экземпляров"},
        {QT_TRANSLATE_N_NOOP("Kinetica", "%n копия"), "копия", "копии", "копий"}
    };
    const auto& words = forms[static_cast<int>(noun)];
    return useEnglish ? QCoreApplication::translate("Kinetica", words.source, nullptr, count)
                      : ruPlural(count, words.one, words.few, words.many);
}
