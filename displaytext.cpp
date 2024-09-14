#include "displaytext.h"
#include "mainwindow.h"
#include <QMouseEvent>
#include <QDateTime>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextBlock>
#include <QMenu>
#include <QAction>

#include "DriftingDateTime.h"

#include "qt_helpers.hpp"

#include "moc_displaytext.cpp"

DisplayText::DisplayText(QWidget *parent)
  : QTextEdit(parent)
  , erase_action_ {new QAction {tr ("&Erase"), this}}
{
  setReadOnly (true);
  setUndoRedoEnabled (false);
  viewport ()->setCursor (Qt::ArrowCursor);
  setWordWrapMode (QTextOption::NoWrap);

  // max lines to limit heap usage
  document ()->setMaximumBlockCount (5000);

  // context menu erase action
  setContextMenuPolicy (Qt::CustomContextMenu);
  connect (this, &DisplayText::customContextMenuRequested, [this] (QPoint const& position) {
      auto * menu = createStandardContextMenu (position);
      menu->addAction (erase_action_);
      menu->exec (mapToGlobal (position));
      delete menu;
    });
  connect (erase_action_, &QAction::triggered, this, &DisplayText::erase);
}

void DisplayText::erase ()
{
  clear ();
  Q_EMIT erased ();
}

void DisplayText::setContentFont(QFont const& font)
{
  char_font_ = font;
  selectAll ();
  auto cursor = textCursor ();
  cursor.beginEditBlock ();
  auto char_format = cursor.charFormat ();
  char_format.setFont (char_font_);
  cursor.mergeCharFormat (char_format);
  cursor.clearSelection ();
  cursor.movePosition (QTextCursor::End);

  // position so viewport scrolled to left
  cursor.movePosition (QTextCursor::Up);
  cursor.movePosition (QTextCursor::StartOfLine);
  cursor.endEditBlock ();

  setTextCursor (cursor);
  ensureCursorVisible ();
}

void DisplayText::mouseDoubleClickEvent(QMouseEvent *e)
{
  Q_EMIT selectCallsign(e->modifiers ());
  QTextEdit::mouseDoubleClickEvent(e);
}

void DisplayText::insertLineSpacer(QString const& line)
{
  appendText (line, "#d3d3d3");
}

void DisplayText::appendText(QString const& text, QColor bg, QString const& call1, QString const& call2)
{
  auto cursor = textCursor ();
  cursor.movePosition (QTextCursor::End);
  auto block_format = cursor.blockFormat ();
  block_format.setBackground (bg);
  if (0 == cursor.position ())
    {
      cursor.setBlockFormat (block_format);
      auto char_format = cursor.charFormat ();
      char_format.setFont (char_font_);
      cursor.setCharFormat (char_format);
    }
  else
    {
      cursor.insertBlock (block_format);
    }

  QTextCharFormat format = cursor.charFormat();
  format.clearBackground();
  int text_index {0};
  if (call1.size ())
    {
      auto call_index = text.indexOf (call1);
      if (call_index != -1) // sanity check
        {
          auto pos = highlighted_calls_.find (call1);
          if (pos != highlighted_calls_.end ())
            {
              cursor.insertText(text.left (call_index), format);
              if (pos.value ().first.isValid ())
                {
                  format.setBackground (pos.value ().first);
                }
              if (pos.value ().second.isValid ())
                {
                  format.setForeground (pos.value ().second);
                }
              cursor.insertText(text.mid (call_index, call1.size ()), format);
              text_index = call_index + call1.size ();
            }
        }
    }
  if (call2.size ())
    {
      auto call_index = text.indexOf (call2, text_index);
      if (call_index != -1) // sanity check
        {
          auto pos = highlighted_calls_.find (call2);
          if (pos != highlighted_calls_.end ())
            {
              format.setBackground (bg);
              format.clearForeground ();
              cursor.insertText(text.mid (text_index, call_index - text_index), format);
              if (pos.value ().second.isValid ())
                {
                  format.setBackground (pos.value ().first);
                }
              if (pos.value ().second.isValid ())
                {
                  format.setForeground (pos.value ().second);
                }
              cursor.insertText(text.mid (call_index, call2.size ()), format);
              text_index = call_index + call2.size ();
            }
        }
    }
  format.setBackground (bg);
  format.clearForeground ();
  cursor.insertText(text.mid (text_index), format);

  // position so viewport scrolled to left
  cursor.movePosition (QTextCursor::StartOfLine);
  setTextCursor (cursor);
  ensureCursorVisible ();
  document ()->setMaximumBlockCount (document ()->maximumBlockCount ());
}


QString DisplayText::appendDXCCWorkedB4(QString message, QString const& callsign, QColor * bg,
          LogBook const& logBook, QColor color_CQ,
          QColor color_DXCC,
          QColor color_NewCall)
{
  // allow for seconds
  int padding {message.indexOf (" ") > 4 ? 2 : 0};
  QString call = callsign;
  QString countryName;
  bool callWorkedBefore;
  bool countryWorkedBefore;

  if(call.length()==2) {
    int i0=message.indexOf("CQ "+call);
    call=message.mid(i0+6,-1);
    i0=call.indexOf(" ");
    call=call.mid(0,i0);
  }
  if(call.length()<3) return message;
  if(!call.contains(QRegularExpression("[0-9]|[A-Z]"))) return message;

  logBook.match(/*in*/call,/*out*/countryName,callWorkedBefore,countryWorkedBefore);
  message = message.trimmed ();
  QString appendage;
  if (!countryWorkedBefore) // therefore not worked call either
    {
      appendage += "!";
      *bg = color_DXCC;
    }
  else
    {
      if (!callWorkedBefore) // but have worked the country
        {
          appendage += "~";
          *bg = color_NewCall;
        }
      else
        {
          appendage += " ";  // have worked this call before
          *bg = color_CQ;
        }
    }

  int i1=countryName.indexOf(";");
  if(m_bPrincipalPrefix) {
    int i2=countryName.lastIndexOf(";");
    if(i1>0) countryName=countryName.mid(i1+2,i2-i1-2);
  } else {
    if(i1>0) countryName=countryName.mid(0,i1);
  // do some obvious abbreviations
    countryName.replace ("Islands", "Is.");
    countryName.replace ("Island", "Is.");
    countryName.replace ("North ", "N. ");
    countryName.replace ("Northern ", "N. ");
    countryName.replace ("South ", "S. ");
    countryName.replace ("East ", "E. ");
    countryName.replace ("Eastern ", "E. ");
    countryName.replace ("West ", "W. ");
    countryName.replace ("Western ", "W. ");
    countryName.replace ("Central ", "C. ");
    countryName.replace (" and ", " & ");
    countryName.replace ("Republic", "Rep.");
    countryName.replace ("United States", "U.S.A.");
    countryName.replace ("Fed. Rep. of ", "");
    countryName.replace ("French ", "Fr.");
    countryName.replace ("Asiatic", "AS");
    countryName.replace ("European", "EU");
    countryName.replace ("African", "AF");
  }

  appendage += countryName;

  // use a nbsp to save the start of appended text so we can find
  // it again later, align appended data at a fixed column if
  // there is space otherwise let it float to the right
  if (const auto space_count = 40 + padding - message.size(); space_count > 0)
  {
     message += QString {space_count, QChar {' '}};
  }
  message += QChar::Nbsp + appendage;

  return message;
}

void DisplayText::displayDecodedText(DecodedText const& decodedText, QString const& myCall,
                                     bool displayDXCCEntity, LogBook const& logBook,
                                     QColor color_CQ, QColor color_MyCall,
                                     QColor color_DXCC, QColor color_NewCall, bool ppfx,
                                     bool bCQonly)
{
  m_bPrincipalPrefix=ppfx;
  QColor bg {Qt::transparent};
  bool CQcall = false;
  if (decodedText.string ().contains (" CQ ")
      || decodedText.string ().contains (" CQDX ")
      || decodedText.string ().contains (" QRZ "))
    {
      CQcall = true;
      bg = color_CQ;
    }
  if(bCQonly and !CQcall) return;
  if (myCall != "" and (
                        decodedText.indexOf (" " + myCall + " ") >= 0
                        or decodedText.indexOf (" " + myCall + "/") >= 0
                        or decodedText.indexOf ("/" + myCall + " ") >= 0
                        or decodedText.indexOf ("<" + myCall + " ") >= 0
                        or decodedText.indexOf (" " + myCall + ">") >= 0)) {
    bg = color_MyCall;
  }
  auto message = decodedText.string ();
  QString dxCall;
  QString dxGrid;
  decodedText.deCallAndGrid (dxCall, dxGrid);
  message = message.left (message.indexOf (QChar::Nbsp)); // strip appended info
  if (displayDXCCEntity && CQcall)
    // if enabled add the DXCC entity and B4 status to the end of the
    // preformated text line t1
    message = appendDXCCWorkedB4 (message, decodedText.CQersCall (), &bg, logBook, color_CQ,
                                  color_DXCC, color_NewCall);
  appendText (message.trimmed (), bg, decodedText.call (), dxCall);
}


void DisplayText::displayTransmittedText(QString text, QString modeTx, qint32 txFreq,
                                         QColor color_ReceivedMsg, bool bFastMode)
{
    QString t1=" @  ";
    if(modeTx=="FT8") t1=" ~  ";
    if(modeTx=="JT4") t1=" $  ";
    if(modeTx=="JT65") t1=" #  ";
    if(modeTx=="MSK144") t1=" &  ";
    QString t2 = QString("%1").arg(txFreq, 4);
    QString t;
    if(bFastMode or modeTx=="FT8") {
      t = DriftingDateTime::currentDateTimeUtc().toString("hhmmss") + \
        "  Tx      " + t2 + t1 + text;
    } else if(modeTx.mid(0,6)=="FT8fox") {
      t = DriftingDateTime::currentDateTimeUtc().toString("hhmmss") + \
        " Tx" + modeTx.mid(7) + " " + text;
    } else {
      t = DriftingDateTime::currentDateTimeUtc().toString("hhmm") + \
        "  Tx      " + t2 + t1 + text;
    }
    appendText (t, color_ReceivedMsg);
}

void DisplayText::displayQSY(QString text)
{
  QString t = DriftingDateTime::currentDateTimeUtc().toString("hhmmss") + "            " + text;
  appendText (t, "hotpink");
}

void DisplayText::displayFoxToBeCalled(QString t, QColor bg)
{
  appendText(t,bg);
}

namespace
{
  void update_selection (QTextCursor& cursor, QColor const& bg, QColor const& fg)
  {
    if (!cursor.isNull ())
      {
        QTextCharFormat format {cursor.charFormat ()};
        if (bg.isValid ())
          {
            format.setBackground (bg);
          }
        else
          {
            format.clearBackground ();
          }
        if (fg.isValid ())
          {
            format.setForeground (fg);
          }
        else
          {
            format.clearForeground ();
          }
        cursor.mergeCharFormat (format);
      }
  }

  void reset_selection (QTextCursor& cursor)
  {
    if (!cursor.isNull ())
      {
        // restore previous text format, we rely on the text
        // char format at he start of the selection being the
        // old one which should be the case
        auto c2 = cursor;
        c2.setPosition (c2.selectionStart ());
        cursor.setCharFormat (c2.charFormat ());
      }
  }
}

void DisplayText::highlight_callsign (QString const& callsign, QColor const& bg, QColor const& fg, bool last_only)
{
  QTextCharFormat old_format {currentCharFormat ()};
  QTextCursor cursor {document ()};
  if (last_only)
    {
      cursor.movePosition (QTextCursor::End);
      cursor = document ()->find (callsign, cursor
                                  , QTextDocument::FindBackward | QTextDocument::FindWholeWords);
      if (bg.isValid () || fg.isValid ())
        {
          update_selection (cursor, bg, fg);
        }
      else
        {
          reset_selection (cursor);
        }
    }
  else
    {
      auto pos = highlighted_calls_.find (callsign);
      if (bg.isValid () || fg.isValid ())
        {
          auto colours = qMakePair (bg, fg);
          if (pos == highlighted_calls_.end ())
            {
              pos = highlighted_calls_.insert (callsign.toUpper (), colours);
            }
          else
            {
              pos.value () = colours; // update colours
            }
          while (!cursor.isNull ())
            {
              cursor = document ()->find (callsign, cursor, QTextDocument::FindWholeWords);
              update_selection (cursor, bg, fg);
            }
        }
      else if (pos != highlighted_calls_.end ())
        {
          highlighted_calls_.erase (pos);
          QTextCursor cursor {document ()};
          while (!cursor.isNull ())
            {
              cursor = document ()->find (callsign, cursor, QTextDocument::FindWholeWords);
              reset_selection (cursor);
            }
        }
    }
  setCurrentCharFormat (old_format);
}
