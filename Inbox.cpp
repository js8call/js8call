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

#include "Inbox.h"
#include <QDebug>

namespace
{
    constexpr char SCHEMA[] = "CREATE TABLE IF NOT EXISTS inbox_v1 ("
                              "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
                              "  blob TEXT"
                              ");"
                              "CREATE INDEX IF NOT EXISTS idx_inbox_v1__type ON"
                              "  inbox_v1(json_extract(blob, '$.type'));"
                              "CREATE INDEX IF NOT EXISTS idx_inbox_v1__params_from ON"
                              "  inbox_v1(json_extract(blob, '$.params.FROM'));"
                              "CREATE INDEX IF NOT EXISTS idx_inbox_v1__params_to ON"
                              "  inbox_v1(json_extract(blob, '$.params.TO'))";

    // Attempt to retrieve a Mesage object previously serialized as a
    // JSON object to the specified column; will throw on failure to
    // deserialize the object.

    auto
    get_column_message(sqlite3_stmt * const stmt,
                       int            const iCol)
    {
        return Message::fromJson(QByteArray((const char *)sqlite3_column_text (stmt, iCol),
                                                          sqlite3_column_bytes(stmt, iCol)));
    }
}

Inbox::Inbox(QString path) :
    path_{ path },
    db_{ nullptr }
{
}

Inbox::~Inbox(){
    close();
}


/**
 * Low-Level Interface
 **/

bool Inbox::isOpen(){
    return db_ != nullptr;
}

bool Inbox::open(){
    int rc = sqlite3_open(path_.toLocal8Bit().data(), &db_);
    if(rc != SQLITE_OK){
        close();
        return false;
    }

    rc = sqlite3_exec(db_, SCHEMA, nullptr, nullptr, nullptr);
    if(rc != SQLITE_OK){
        return false;
    }

    return true;
}

void Inbox::close(){
    if(db_){
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

QString Inbox::error(){
    if(db_){
        return QString::fromLocal8Bit(sqlite3_errmsg(db_));
    }
    return "";
}

int Inbox::count(QString type, QString query, QString match){
    if(!isOpen()){
        return -1;
    }

    const char* sql = "SELECT COUNT(*) FROM inbox_v1 "
                      "WHERE json_extract(blob, '$.type') = ? "
                      "AND json_extract(blob, ?) LIKE ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return -1;
    }

    auto t8 = type.toLocal8Bit();
    auto q8 = query.toLocal8Bit();
    auto m8 = match.toLocal8Bit();
    rc = sqlite3_bind_text(stmt, 1, t8.data(),  -1, nullptr);
    rc = sqlite3_bind_text(stmt, 2, q8.data(), -1, nullptr);
    rc = sqlite3_bind_text(stmt, 3, m8.data(), -1, nullptr);

    int count = 0;
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return -1;
    }

    return count;
}

QList<QPair<int, Message> > Inbox::values(QString type, QString query, QString match, int offset, int limit){
    if(!isOpen()){
        return {};
    }

    const char* sql = "SELECT id, blob FROM inbox_v1 "
                      "WHERE json_extract(blob, '$.type') = ? "
                      "AND json_extract(blob, ?) LIKE ? "
                      "ORDER BY id ASC "
                      "LIMIT ? OFFSET ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return {};
    }

    auto t8 = type.toLocal8Bit();
    auto q8 = query.toLocal8Bit();
    auto m8 = match.toLocal8Bit();
    rc = sqlite3_bind_text(stmt, 1, t8.data(),  -1, nullptr);
    rc = sqlite3_bind_text(stmt, 2, q8.data(), -1, nullptr);
    rc = sqlite3_bind_text(stmt, 3, m8.data(), -1, nullptr);
    rc = sqlite3_bind_int(stmt, 4, limit);
    rc = sqlite3_bind_int(stmt, 5, offset);

    //qDebug() << "exec" << sqlite3_expanded_sql(stmt);

    QList<QPair<int, Message>> v;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        try
        {
            v.append({
                sqlite3_column_int(stmt, 0),
                get_column_message(stmt, 1)
            });
        }
        catch (...)
        {
            continue;
        }
    }

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return {};
    }

    return v;
}

Message Inbox::value(int key){
    if(!isOpen()){
        return {};
    }

    const char* sql = "SELECT blob FROM inbox_v1 WHERE id = ? LIMIT 1;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return {};
    }

    rc = sqlite3_bind_int(stmt, 1, key);

    Message m;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        try
        {
            m = get_column_message(stmt, 0);
        }
        catch (...)
        {
            continue;
        }
    }

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return {};
    }

    return m;
}

int Inbox::append(Message value){
    if(!isOpen()){
        return -1;
    }

    const char* sql = "INSERT INTO inbox_v1 (blob) VALUES (?);";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return -2;
    }

    auto j8 = value.toJson();
    rc = sqlite3_bind_text(stmt, 1, j8.data(), -1, nullptr);
    rc = sqlite3_step(stmt);

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return -1;
    }

    return sqlite3_last_insert_rowid(db_);
}

bool Inbox::set(int key, Message value){
    if(!isOpen()){
        return false;
    }

    const char* sql = "UPDATE inbox_v1 SET blob = ? WHERE id = ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return false;
    }

    auto j8 = value.toJson();
    rc = sqlite3_bind_text(stmt, 1, j8.data(), -1, nullptr);
    rc = sqlite3_bind_int(stmt, 2, key);

    rc = sqlite3_step(stmt);

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return false;
    }

    return true;
}

bool Inbox::del(int key){
    if(!isOpen()){
        return false;
    }

    const char* sql = "DELETE FROM inbox_v1 WHERE id = ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return false;
    }

    rc = sqlite3_bind_int(stmt, 1, key);
    rc = sqlite3_step(stmt);

    rc = sqlite3_finalize(stmt);
    if(rc != SQLITE_OK){
        return false;
    }

    return true;
}

/**
 * High-Level Interface
 **/

int Inbox::countUnreadFrom(QString from){
    return count("UNREAD", "$.params.FROM", from);
}

QPair<int, Message> Inbox::firstUnreadFrom(QString from){
    auto v = values("UNREAD", "$.params.FROM", from, 0, 1);
    if(v.isEmpty()){
        return {};
    }
    return v.first();
}

