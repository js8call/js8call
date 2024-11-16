/**
 * This file is part of JS8Call.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 *
 **/

#include "jsc_checker.h"

#include <QTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QDebug>

#include "jsc.h"
#include "varicode.h"

const int CORRECT = QTextFormat::UserProperty + 10;
const QString ALPHABET = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ" };

JSCChecker::JSCChecker(QObject *parent) :
    QObject(parent)
{
}

bool cursorHasProperty(const QTextCursor &cursor, int property){
    if(property < QTextFormat::UserProperty) {
        return false;
    }
    if(cursor.charFormat().intProperty(property) == 1) {
        return true;
    }
    auto const & formats = cursor.block().layout()->formats();
    int pos = cursor.positionInBlock();
    foreach(auto const & range, formats) {
        if(pos > range.start && pos <= range.start + range.length && range.format.intProperty(property) == 1) {
            return true;
        }
    }
    return false;
}

QString nextChar(QTextCursor c){
    QTextCursor cur(c);
    cur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return cur.selectedText().toUpper();
}

bool isNumeric(QString s){
    return s.indexOf(QRegularExpression("^\\d+$")) == 0;
}

bool isWordChar(QString ch){
    return ch.contains(QRegularExpression("^\\w$"));
}

void JSCChecker::checkRange(QTextEdit* edit, int start, int end)
{
    if(end == -1){
        QTextCursor tmpCursor(edit->textCursor());
        tmpCursor.movePosition(QTextCursor::End);
        end = tmpCursor.position();
    }

    // stop contentsChange signals from being emitted due to changed charFormats
    edit->document()->blockSignals(true);

    //qDebug() << "checking range " << start << " - " << end;

    QTextCharFormat errorFmt;
    errorFmt.setFontUnderline(true);
    errorFmt.setUnderlineColor(Qt::red);
    errorFmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    QTextCharFormat defaultFormat = QTextCharFormat();

    auto cursor = edit->textCursor();

    cursor.beginEditBlock();
    {
        cursor.setPosition(start);
        while(cursor.position() < end) {
            bool correct = false;

            cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            if(cursor.selectedText()/*.toUpper()*/ == "@"){
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            }

            if(cursorHasProperty(cursor, CORRECT)){
                correct = true;
            } else {
                QString word = cursor.selectedText().toUpper();

                // three or less is always "correct"
                if(word.length() < 4 || isNumeric(word)){
                    correct = true;
                } else {
                    bool found = false;
                    quint32 index = JSC::lookup(word, &found);
                    if(found){
                        correct = JSC::map[index].size == word.length();
                    }

                    if(!correct){
                        correct = Varicode::isValidCallsign(word, nullptr);
                    }
                }

                //qDebug() << "word" << word << "correct" << correct;
            }

            if(correct){
                QTextCharFormat fmt = cursor.charFormat();
                fmt.setFontUnderline(defaultFormat.fontUnderline());
                fmt.setUnderlineColor(defaultFormat.underlineColor());
                fmt.setUnderlineStyle(defaultFormat.underlineStyle());
                cursor.setCharFormat(fmt);
            } else {
                cursor.mergeCharFormat(errorFmt);
            }

            // Go to next word start
            //while(cursor.position() < end && !isWordChar(nextChar(cursor))){
            //    cursor.movePosition(QTextCursor::NextCharacter);
            //}
            cursor.movePosition(QTextCursor::NextCharacter);
        }
    }
    cursor.endEditBlock();

    edit->document()->blockSignals(false);
}

QSet<QString> oneEdit(QString word, bool includeAdditions, bool includeDeletions){
    QSet<QString> all;

    // 1-edit distance words (i.e., prefixed/suffixed/edited characters)
    for(int i = 0; i < 26; i++){
        if(includeAdditions){
            auto prefixed = ALPHABET.mid(i, 1) + word;
            all.insert(prefixed);

            auto suffixed = word + ALPHABET.mid(i, 1);
            all.insert(suffixed);
        }

        for(int j = 0; j < word.length(); j++){
            auto edited = word.mid(0, j) + ALPHABET.mid(i, 1) + word.mid(j + 1, word.length() - j);
            all.insert(edited);
        }
    }

    // 1-edit distance words (i.e., removed characters)
    if(includeDeletions){
        for(int j = 0; j < word.length(); j++){
            auto deleted = word.mid(0, j) + word.mid(j + 1, word.length() - j);
            all.insert(deleted);
        }
    }

    return all;
}

QMultiMap<quint32, QString> candidates(QString word, bool includeTwoEdits){
    // one edit
    QSet<QString> one = oneEdit(word, true, true);

    // two edits
    QSet<QString> two;
    if(includeTwoEdits){
        foreach(auto w, one){
            two |= oneEdit(w, false, false);
        }
    }

    // existence check
    QMultiMap<quint32, QString> m;

    quint32 index;
    foreach(auto w, one | two){
        if(JSC::exists(w, &index)){
            m.insert(index, w);
        }
    }

    return m;
}

QStringList JSCChecker::suggestions(QString word, int n, bool *pFound){
    QStringList s;

    // qDebug() << "computing suggestions for word" << word;

    QMultiMap<quint32, QString> m;

    bool prefixFound = false;

    // lookup actual word prefix that is not a single character
    quint32 index = JSC::lookup(word, &prefixFound);
    if(prefixFound){
        auto t = JSC::map[index];
        if(t.size > 1){
            m.insert(index, QString::fromLatin1(t.str, t.size));
        }
    }

    // compute suggestion candidates
    m.unite(candidates(word, false));

    // return in order of probability (i.e., index rank)
    int i = 0;
    foreach(auto key, m.uniqueKeys()){
        if(i >= n){
            break;
        }
        //qDebug() << "suggest" << m[key] << key;
        s.append(m.values(key));
        i++;
    }

    if(pFound) *pFound = prefixFound;

    return s;
}
