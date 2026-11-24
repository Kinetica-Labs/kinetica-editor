// Letter keys by their place on the keyboard, not by the layout: with the Russian layout on, the key a QWERTY board
// prints W on arrives as Ц (QKeyEvent::key() is the layout's letter), and a check for Qt::Key_W misses it - the
// flight's W A S D did nothing for a user typing Russian. Window shortcuts already match across
// layouts (Qt tries the Latin letter), so the view has to as well. JCUKEN rows mapped onto QWERTY, as the keys sit.
#pragma once

#include <QKeyEvent>

inline int keyByPlace(int key) {
    static const char16_t ru[] = u"ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ";
    static const char latin[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    for (int i = 0; i < 26; ++i)
        if (key == int(ru[i])) return int(latin[i]); // Qt's letter keys are the upper-case code points
    return key;
}

inline int keyByPlace(const QKeyEvent* e) { return keyByPlace(e->key()); }
