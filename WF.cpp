#include "WF.hpp"

#include <stdexcept>
#include <memory>
#include <algorithm>

#include <QMetaType>
#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QColorDialog>
#include <QColor>
#include <QBrush>
#include <QPoint>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QStandardPaths>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDebug>

#include "qt_helpers.hpp"

#include "ui_wf_palette_design_dialog.h"

namespace
{
  int constexpr points {256};

  using Colours = WF::Palette::Colours;

  // ensure that palette colours are useable for interpolation
  Colours make_valid (Colours colours)
  {
    if (colours.size () < 2)
      {
        // allow single element by starting at black
        colours.prepend (QColor {0, 0, 0});
      }

    if (1 == colours.size ())
      {
        // allow empty list by using black to white
        colours.append (QColor {255,255,255});
      }

    if (colours.size () > points)
      {
        throw_qstring (QObject::tr ("Too many colours in palette."));
      }

    return colours;
  }

  // load palette colours from a file
  Colours load_palette (QString const& file_name)
  {
    Colours colours;
    QFile file {file_name};
    if (file.open (QIODevice::ReadOnly))
      {
        unsigned count {0};
        QTextStream in (&file);
        int line_counter {0};
        while (!in.atEnd ())
          {
            auto line = in.readLine();
            ++line_counter;

            if (++count >= points)
              {
                throw_qstring (QObject::tr ("Error reading waterfall palette file \"%1:%2\" too many colors.")
                               .arg (file.fileName ()).arg (line_counter));
              }
            auto items = line.split (';');
            if (items.size () != 3)
              {
                throw_qstring (QObject::tr ("Error reading waterfall palette file \"%1:%2\" invalid triplet.")
                               .arg (file.fileName ()).arg (line_counter));
              }
            bool r_ok, g_ok, b_ok;
            auto r = items[0].toInt (&r_ok);
            auto g = items[1].toInt (&g_ok);
            auto b = items[2].toInt (&b_ok);
            if (!r_ok || !g_ok || !b_ok
                || r < 0 || r > 255
                || g < 0 || g > 255
                || b < 0 || b > 255)
              {
                throw_qstring (QObject::tr ("Error reading waterfall palette file \"%1:%2\" invalid color.")
                               .arg (file.fileName ()).arg (line_counter));
              }
            colours.append (QColor {r, g, b});
          }
      }
    else
      {
        throw_qstring (QObject::tr ("Error opening waterfall palette file \"%1\": %2.").arg (file.fileName ()).arg (file.errorString ()));
      }

    return colours;
  }

  // GUI to design and manage waterfall palettes
  class Designer
    : public QDialog
  {
    Q_OBJECT;

  public:
    explicit Designer (Colours const& current, QWidget * parent = nullptr)
      : QDialog {parent}
      , colours_ {current}
    {
      ui_.setupUi (this);

      // context menu actions
      auto import_button = ui_.button_box->addButton ("&Import...", QDialogButtonBox::ActionRole);
      connect (import_button, &QPushButton::clicked, this, &Designer::import_palette);

      auto export_button = ui_.button_box->addButton ("&Export...", QDialogButtonBox::ActionRole);
      connect (export_button, &QPushButton::clicked, this, &Designer::export_palette);

      // hookup the context menu handler
      connect (ui_.colour_table_widget, &QWidget::customContextMenuRequested, this, &Designer::context_menu);

      load_table ();
    }

    void load_table ()
    {
      // load the table items
      ui_.colour_table_widget->clear ();
      ui_.colour_table_widget->setRowCount (colours_.size ());
      for (int i {0}; i < colours_.size (); ++i)
        {
          insert_item (i);
        }
    }

    Colours colours () const
    {
      return colours_;
    }

    // invoke the colour editor
    Q_SLOT void on_colour_table_widget_itemDoubleClicked (QTableWidgetItem * item)
    {
      auto new_colour = QColorDialog::getColor (item->background ().color (), this);
      if (new_colour.isValid ())
        {
          item->setBackground (QBrush {new_colour});
          colours_[item->row ()] = new_colour;
        }
    }

  private:
    void insert_item (int row)
    {
      std::unique_ptr<QTableWidgetItem> item {new QTableWidgetItem {""}};
      item->setBackground (QBrush {colours_[row]});
      item->setFlags (Qt::ItemIsEnabled);
      ui_.colour_table_widget->setItem (row, 0, item.release ());
    }

    void insert_new_item (int row, QColor const& default_colour)
    {
      // use the prior row colour as default if available
      auto new_colour = QColorDialog::getColor (row > 0 ? colours_[row - 1] : default_colour, this);
      if (new_colour.isValid ())
        {
          ui_.colour_table_widget->insertRow (row);
          colours_.insert (row, new_colour);
          insert_item (row);
        }
    }

    void context_menu (QPoint const& p)
    {
      context_menu_.clear ();
      if (ui_.colour_table_widget->itemAt (p))
        {
          auto delete_action = context_menu_.addAction (tr ("&Delete"));
          connect (delete_action, &QAction::triggered, [this] ()
                   {
                     auto row = ui_.colour_table_widget->currentRow ();
                     ui_.colour_table_widget->removeRow (row);
                     colours_.removeAt (row);
                   });
        }

      auto insert_action = context_menu_.addAction (tr ("&Insert ..."));
      connect (insert_action, &QAction::triggered, [this] ()
               {
                 auto item = ui_.colour_table_widget->itemAt (menu_pos_);
                 int row = item ? item->row () : colours_.size ();
                 insert_new_item (row, QColor {0, 0, 0});
               });

      auto insert_after_action = context_menu_.addAction (tr ("Insert &after ..."));
      connect (insert_after_action, &QAction::triggered, [this] ()
               {
                 auto item = ui_.colour_table_widget->itemAt (menu_pos_);
                 int row = item ? item->row () + 1 : colours_.size ();
                 insert_new_item( row, QColor {255, 255, 255});
               });

      menu_pos_ = p;            // save for context menu action handlers
      context_menu_.popup (ui_.colour_table_widget->mapToGlobal (p));
    }

    void import_palette ()
    {
      auto docs = QStandardPaths::writableLocation (QStandardPaths::DocumentsLocation);
      auto file_name = QFileDialog::getOpenFileName (this, tr ("Import Palette"), docs, tr ("Palettes (*.pal)"));
      if (!file_name.isEmpty ())
        {
          colours_ = load_palette (file_name);
          load_table ();
        }
    }

    void export_palette ()
    {
      auto docs = QStandardPaths::writableLocation (QStandardPaths::DocumentsLocation);
      auto file_name = QFileDialog::getSaveFileName (this, tr ("Export Palette"), docs, tr ("Palettes (*.pal)"));
      if (!file_name.isEmpty ())
        {
          if (!QFile::exists (file_name) && !file_name.contains ('.'))
            {
              file_name += ".pal";
            }
          QFile file {file_name};
          if (file.open (QFile::WriteOnly | QFile::Truncate | QFile::Text))
            {
              QTextStream stream {&file};
              Q_FOREACH (auto colour, colours_)
                {
                  stream << colour.red () << ';' << colour.green () << ';' << colour.blue () << Qt::endl;
                }
            }
          else
            {
              throw_qstring (QObject::tr ("Error writing waterfall palette file \"%1\": %2.").arg (file.fileName ()).arg (file.errorString ()));
            }
        }
    }

    Ui::wf_palette_design_dialog ui_;
    Colours colours_;
    QMenu context_menu_;
    QPoint menu_pos_;
  };
}

#include "WF.moc"

namespace WF
{
  Palette::Palette (QString const& file_path)
    : colours_ {load_palette (file_path)}
  {
  }

  Palette::Palette (Colours const& colour_list)
    : colours_ {colour_list}
  {
  }

    // generate an array of colours suitable for the waterfall plotter
  QVector<QColor> Palette::interpolate () const
  {
    Colours colours {make_valid (colours_)};
    QVector<QColor> result;
    result.reserve (points);

    // do a linear-ish gradient between each supplied colour point
    auto interval = qreal (points) / (colours.size () - 1);

    for (int i {0}; i < points; ++i)
      {
        int prior = i / interval;

        if (prior >= (colours.size () - 1))
          {
            --prior;
          }
        auto next = prior + 1;
        if (next >= colours.size ())
          {
            --next;
          }

        // qDebug () << "Palette::interpolate: prior:" << prior << "total:" << colours.size ();

        auto increment = i - qreal (interval) * prior;
        qreal r {colours[prior].redF () + (increment * (colours[next].redF () - colours[prior].redF ()))/interval};
        qreal g {colours[prior].greenF () + (increment * (colours[next].greenF () - colours[prior].greenF ()))/interval};
        qreal b {colours[prior].blueF () + (increment * (colours[next].blueF () - colours[prior].blueF ()))/interval};
        result.append (QColor::fromRgbF (r, g, b));

        // qDebug () << "Palette colour[" << (result.size () - 1) << "] =" << result[result.size () - 1] << "from: r:" << r << "g:" << g << "b:" << b;
      }

    return result;
  }

    // invoke the palette designer
  bool Palette::design ()
  {
    if (auto designer = Designer{colours_};
             designer.exec() == QDialog::Accepted)
      {
        colours_ = designer.colours ();
        return true;
      }
    return false;
  }
}
