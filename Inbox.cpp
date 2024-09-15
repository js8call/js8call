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
#include "DriftingDateTime.h"

#include <QDebug>


const char* SCHEMA = "CREATE TABLE IF NOT EXISTS inbox_v1 ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
                     "  blob TEXT"
                     ");"
                     "CREATE INDEX IF NOT EXISTS idx_inbox_v1__type ON"
                     "  inbox_v1(json_extract(blob, '$.type'));"
                     "CREATE INDEX IF NOT EXISTS idx_inbox_v1__params_from ON"
                     "  inbox_v1(json_extract(blob, '$.params.FROM'));"
                     "CREATE INDEX IF NOT EXISTS idx_inbox_v1__params_to ON"
                     "  inbox_v1(json_extract(blob, '$.params.TO'));"
					 "CREATE TABLE IF NOT EXISTS inbox_group_recip_v1 ("
					 "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
					 "  msg_id INTEGER, "
					 "  callsign VARCHAR(255), "
					 "  FOREIGN KEY(msg_id) REFERENCES inbox_v1(id) ON DELETE CASCADE"
					 ");"
					 "CREATE INDEX IF NOT EXISTS idx_inbox_group_recip_v1__callsign ON"
					 "  inbox_group_recip_v1(callsign);";

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

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Message m;

        int i = sqlite3_column_int(stmt, 0);

        auto msg = QByteArray((const char*)sqlite3_column_text(stmt, 1), sqlite3_column_bytes(stmt, 1));

        QJsonParseError e;
        QJsonDocument d = QJsonDocument::fromJson(msg, &e);
        if(e.error != QJsonParseError::NoError){
            continue;
        }

        if(!d.isObject()){
            continue;
        }

        m.read(d.object());
        v.append({ i, m });
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
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto msg = QByteArray((const char*)sqlite3_column_text(stmt, 0), sqlite3_column_bytes(stmt, 0));

        QJsonParseError e;
        QJsonDocument d = QJsonDocument::fromJson(msg, &e);
        if(e.error != QJsonParseError::NoError){
            return {};
        }

        if(!d.isObject()){
            return {};
        }

        m.read(d.object());
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

QMap<QString, int> Inbox::getGroupMessageCounts()
{
	if(!isOpen()){
		return {};
	}

	QMap<QString, int> messageCounts;

	const char* sql = "SELECT count(id) as msg_count, json_extract(blob, '$.params.TO') as group_name FROM inbox_v1 "
					  "WHERE json_extract(blob, '$.type') = 'STORE' "
					  "AND group_name LIKE '@%' "
					  "AND json_extract(blob, '$.params.UTC') > ? "
					  "GROUP BY group_name";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
	if(rc != SQLITE_OK){
		return messageCounts;
	}

	// Set a floor or 48 hours for group message retrieval
	// TODO: date formatting with the "yyyy-MM-dd HH:mm:ss" string happens elsewhere as well, centralize
	// TODO: possibly make the date floor configurable
	auto d8 = DriftingDateTime::currentDateTimeUtc().addDays(-2).toString("yyyy-MM-dd HH:mm:ss").toLocal8Bit();

	rc = sqlite3_bind_text(stmt, 1, d8.data(), -1, nullptr);

	//qDebug() << "exec " << sqlite3_expanded_sql(stmt);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Message m;

		int count = sqlite3_column_int(stmt, 0);
		auto group = sqlite3_column_text(stmt, 1);

		messageCounts.insert(QString::fromLocal8Bit(reinterpret_cast<const char *>(group)), count);
	}

	rc = sqlite3_finalize(stmt);

	return messageCounts;
}

bool Inbox::markGroupMsgDeliveredForCallsign(int msgId, const QString &callsign)
{
	if(!isOpen()){
		return false;
	}

	const char* sql = "SELECT count(id) as msg_count FROM inbox_group_recip_v1 WHERE msg_id = ? AND callsign = ? LIMIT 1;";

	sqlite3_stmt *exists_stmt;
	int rc = sqlite3_prepare_v2(db_, sql, -1, &exists_stmt, nullptr);
	if(rc != SQLITE_OK){
		return false;
	}

	auto cs8 = callsign.toLocal8Bit();

	rc = sqlite3_bind_int(exists_stmt, 1, msgId);
	rc = sqlite3_bind_text(exists_stmt, 2, cs8.data(), -1, nullptr);

	bool recordExists = false;
	rc = sqlite3_step(exists_stmt);
	int count = sqlite3_column_int(exists_stmt, 0);
	recordExists = (count > 0);

	rc = sqlite3_finalize(exists_stmt);

	if(!recordExists)
	{
		sql = "INSERT INTO inbox_group_recip_v1 (msg_id, callsign) VALUES (?,?);";

		sqlite3_stmt *insert_stmt;
		rc = sqlite3_prepare_v2(db_, sql, -1, &insert_stmt, nullptr);
		if (rc != SQLITE_OK)
		{
			return false;
		}

		rc = sqlite3_bind_int(insert_stmt, 1, msgId);
		rc = sqlite3_bind_text(insert_stmt, 2, cs8.data(), -1, nullptr);

		rc = sqlite3_step(insert_stmt);

		rc = sqlite3_finalize(insert_stmt);
		if (rc != SQLITE_OK)
		{
			return false;
		}
	}

	return true;
}

int Inbox::getNextGroupMessageIdForCallsign(const QString &group_name, const QString &callsign){
	if(!isOpen()){
		return -1;
	}

	const char* sql = "SELECT inbox_v1.id, inbox_v1.blob FROM inbox_v1 "
					  "LEFT JOIN inbox_group_recip_v1 ON (inbox_group_recip_v1.msg_id=inbox_v1.id AND inbox_group_recip_v1.callsign = ?) "
					  "WHERE json_extract(blob, '$.type') = 'STORE' "
					  "AND json_extract(blob, '$.params.TO') LIKE ? "
					  "AND json_extract(blob, '$.params.UTC') > ? "
					  "AND inbox_group_recip_v1.id IS NULL "
					  "ORDER BY inbox_v1.id ASC "
					  "LIMIT ? OFFSET ?;";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
	if(rc != SQLITE_OK){
		return -1;
	}

	auto c8 = callsign.toLocal8Bit();
	auto g8 = group_name.toLocal8Bit();

	// Set a floor or 48 hours for group message retrieval
	// TODO: date formatting with the "yyyy-MM-dd HH:mm:ss" string happens elsewhere as well, centralize
	// TODO: possibly make the date floor configurable
	auto d8 = DriftingDateTime::currentDateTimeUtc().addDays(-2).toString("yyyy-MM-dd HH:mm:ss").toLocal8Bit();

	rc = sqlite3_bind_text(stmt, 1, c8.data(), -1, nullptr);
	rc = sqlite3_bind_text(stmt, 2, g8.data(), -1, nullptr);
	rc = sqlite3_bind_text(stmt, 3, d8.data(), -1, nullptr);
	rc = sqlite3_bind_int(stmt, 4, 10);
	rc = sqlite3_bind_int(stmt, 5, 0);

	//qDebug() << "exec " << sqlite3_expanded_sql(stmt);

	int next_id = -1;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Message m;

		int i = sqlite3_column_int(stmt, 0);

		auto msg = QByteArray((const char*)sqlite3_column_text(stmt, 1), sqlite3_column_bytes(stmt, 1));

		QJsonParseError e;
		QJsonDocument d = QJsonDocument::fromJson(msg, &e);
		if(e.error != QJsonParseError::NoError){
			continue;
		}

		if(!d.isObject()){
			continue;
		}

		m.read(d.object());

		auto params = m.params();
		auto text = params.value("TEXT").toString().trimmed();
		if(!text.isEmpty()){
			next_id = i;
			break;
		}
	}

	rc = sqlite3_finalize(stmt);
	if(rc != SQLITE_OK){
		return -1;
	}

	return next_id;
}