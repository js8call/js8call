#ifndef EVENTFILTER_HPP__
#define EVENTFILTER_HPP__

#include <functional>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>

namespace EventFilter
{
  class FocusOut final : public QObject
  {
  public:

    using Filter = std::function<void()>;

    FocusOut(Filter    filter,
             QObject * parent = nullptr)
    : QObject {parent}
    , filter_ {filter}
    {}

    bool
    eventFilter(QObject *,
                QEvent  * event) override
    {
      if (event->type() == QEvent::FocusOut) filter_();
      return false;
    }

  private:

    Filter filter_;
  };

  class EscapeKeyPress final : public QObject
  {
  public:

    using Filter = std::function<bool(QKeyEvent *)>;

    EscapeKeyPress(Filter    filter,
                   QObject * parent = nullptr)
    : QObject {parent}
    , filter_ {filter}
    {}

    bool eventFilter(QObject *,
                     QEvent  * event) override
    {
      if (event->type() == QEvent::KeyPress)
      {
        if (auto const keyEvent = static_cast<QKeyEvent *>(event);
                       keyEvent->key() == Qt::Key_Escape)
          {
            return filter_(keyEvent);
          }
      }
      return false;
    }

  private:

    Filter filter_;
  };

  class EnterKeyPress final : public QObject
  {
  public:

    using Filter = std::function<bool(QKeyEvent *)>;

     EnterKeyPress(Filter    filter,
                   QObject * parent = nullptr)
    : QObject {parent}
    , filter_ {filter}
    {}

    bool eventFilter(QObject *,
                     QEvent  * const event) override
    {
      if (event->type() == QEvent::KeyPress)
      {
        if (auto const keyEvent = static_cast<QKeyEvent *>(event);
                       keyEvent->key() == Qt::Key_Enter ||
                       keyEvent->key() == Qt::Key_Return)
        {
          return filter_(keyEvent);
        }
      }
      return false;
    }

  private:

    Filter filter_;
  };

  class MouseButtonPress final : public QObject
  {
  public:

    using Filter = std::function<bool(QMouseEvent *)>;

    MouseButtonPress(Filter    filter,
                     QObject * parent = nullptr)
    : QObject {parent}
    , filter_ {filter}
    {}

    bool eventFilter(QObject *,
                     QEvent  * event) override
    {
      if (event->type() == QEvent::MouseButtonPress)
      {
        return filter_(static_cast<QMouseEvent *>(event));
      }
      return false;
    }

  private:

    Filter filter_;
  };

  class MouseButtonDblClick final : public QObject
  {
  public:

    using Filter = std::function<bool(QMouseEvent *)>;

    MouseButtonDblClick(Filter    filter,
                        QObject * parent = nullptr)
    : QObject {parent}
    , filter_ {filter}
    {}

    bool eventFilter(QObject *,
                     QEvent  * event) override
    {
      if (event->type() == QEvent::MouseButtonDblClick)
      {
        return filter_(static_cast<QMouseEvent *>(event));
      }
      return false;
    }

  private:

    Filter filter_;
  };
}


#endif