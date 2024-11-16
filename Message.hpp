#ifndef MESSAGE_HPP
#define MESSAGE_HPP

/**
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 **/

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSharedDataPointer>
#include <QString>
#include <QVariant>

class Message final
{
public:

    // Constructors

    Message();
    Message(QString const & type, QString const & value = {});
    Message(QString const & type, QString const & value, QVariantMap const & params);

    // Copying and moving

    Message            (Message const &);
    Message & operator=(Message const &);
    Message            (Message       &&) noexcept;
    Message & operator=(Message       &&) noexcept;

    // Destructor

    ~Message();

    // Accessors

    qint64      id()     const;
    QString     type()   const;
    QString     value()  const;
    QVariantMap params() const;

    // Manipulators

    qint64 ensureId();
    void   setType (QString const &);
    void   setValue(QString const &);

    // Conversions

    QByteArray    toJson()         const;
    QJsonDocument toJsonDocument() const;
    QJsonObject   toJsonObject()   const;
    QVariantMap   toVariantMap()   const;

    // Deserialization

    static Message fromJson(QByteArray    const &);
    static Message fromJson(QJsonDocument const &);
    static Message fromJson(QJsonObject   const &);

private:

    // Shared data implementation

    struct Data;
    QSharedDataPointer<Data> d_;
};

Q_DECLARE_TYPEINFO(Message, Q_MOVABLE_TYPE);

#endif // MESSAGE_HPP
