#include "DisplayManual.hpp"

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QLocale>

#include "revision_utils.hpp"

#include "pimpl_impl.hpp"

namespace 
{
  class token
    : public QObject
  {
    Q_OBJECT
  public:
    token (QUrl const& url, QString const& lang, QString const& name_we, QObject * parent = nullptr)
      : QObject {parent}
      , url_ {url}
      , lang_ {lang}
      , name_we_ {name_we}
    {
    }

    QUrl url_;
    QString lang_;
    QString name_we_;
  };
}
      
class DisplayManual::impl final
  : public QObject
{
  Q_OBJECT
public:
  impl (QNetworkAccessManager * qnam)
    : qnam_ {qnam}
  {
    connect (qnam_, &QNetworkAccessManager::finished, this, &DisplayManual::impl::reply_finished);
  }

  void display (QUrl const& url, QString const& name_we)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    if (QNetworkAccessManager::Accessible != qnam_->networkAccessible ()) {
      // try and recover network access for QNAM
      qnam_->setNetworkAccessible (QNetworkAccessManager::Accessible);
    }
#endif

    // try and find a localized manual
    auto lang = QLocale::system ().name ();
    // try for language and country first
    auto file = name_we + '_' + lang + '-' + version () + ".html";
    auto target = url.resolved (file);
    QNetworkRequest request {target};
    request.setRawHeader ("User-Agent", "WSJT-X Manual Checker");
    request.setOriginatingObject (new token {url, lang, name_we, this});
    auto * reply = qnam_->head (request);
    outstanding_requests_ << reply;
  }

  void reply_finished (QNetworkReply * reply)
  {
    if (outstanding_requests_.contains (reply))
      {
        QUrl target;
        if (reply->error ())
          {
            if (auto * tok = qobject_cast<token *> (reply->request ().originatingObject ()))
              {
                auto pos = tok->lang_.lastIndexOf ('_');
                QString file;
                if (pos >= 0)
                  {
                    tok->lang_.truncate (pos);
                    file = tok->name_we_ + '_' + tok->lang_ + '-' + version () + ".html";
                    target = tok->url_.resolved (file);
                    QNetworkRequest request {target};
                    request.setRawHeader ("User-Agent", "WSJT-X Manual Checker");
                    request.setOriginatingObject (tok);
                    auto * reply = qnam_->head (request);
                    outstanding_requests_ << reply;
                  }
                else
                  {
                    // give up looking and request the default
                    file = tok->name_we_ + '-' + version () + ".html";
                    target = tok->url_.resolved (file);
                    QDesktopServices::openUrl (target);
                    delete tok;
                  }
              }
          }
        else
          {
            // found it
            if (auto * tok = qobject_cast<token *> (reply->request ().originatingObject ()))
              {
                delete tok;
              }
            QDesktopServices::openUrl (reply->request ().url ());
          }

        outstanding_requests_.removeOne (reply);
        reply->deleteLater ();
      }
  }

  QNetworkAccessManager * qnam_;
  QList<QNetworkReply *> outstanding_requests_;
};

#include "DisplayManual.moc"

DisplayManual::DisplayManual (QNetworkAccessManager * qnam, QObject * parent)
  : QObject {parent}
  , m_ {qnam}
{
}

DisplayManual::~DisplayManual ()
{
}

void DisplayManual::display_html_url (QUrl const& url, QString const& name_we)
{
  m_->display (url, name_we);
}

void DisplayManual::display_html_file (QDir const& dir, QString const& name_we)
{
  // try and find a localized manual
  auto lang = QLocale::system ().name ();
  // try for language and country first
  auto file = dir.absoluteFilePath (name_we + '_' + lang + '-' + version () + ".html");
  if (!QFileInfo::exists (file))
    {
      // try for language
      lang.truncate (lang.lastIndexOf ('_'));
      file = dir.absoluteFilePath (name_we + '_' + lang + '-' + version () + ".html");
      if (!QFileInfo::exists (file))
        {
          // use default
          file = dir.absoluteFilePath (name_we + '-' + version () + ".html");
        }
    }
  // may fail but browser 404 error is a good as anything
  QDesktopServices::openUrl (QUrl {"file:///" + file});
}

