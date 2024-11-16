#include "about.h"
#include <QString>
#include "revision_utils.hpp"
#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);

  ui->labelTxt->setText (QString{"<h2>%1</h2><br />"

                         "JS8Call is a derivative of the WSJT-X application, "
                         "restructured and redesigned for message passing. <br/>"
                         "It is not supported by nor endorsed by the WSJT-X "
                         "development group. <br/>JS8Call is "
                         "licensed under and in accordance with the terms "
                         "of the <a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">GPLv3 license</a>.<br/>"
                         "The source code modifications are public and can be found in <a href=\"https://github.com/js8call/js8call\">this repository</a>.<br/><br/>"

                         "JS8Call is heavily inspired by WSJT-X, Fldigi, "
                         "and FSQCall <br/>and would not exist without the hard work and "
                         "dedication of the many <br/>developers in the amateur radio "
                         "community.<br /><br />"
                         "JS8Call stands on the shoulder of giants...the takeoff angle "
                         "is better up there.<br /><br />"
                         "A special thanks goes out to:<br/><br/><strong>"
                         "KC9QNE, "
                         "KI6SSI, "
                         "K0OG, "
                         "LB9YH, "
                         "M0IAX, "
                         "N0JDS, "
                         "OH8STN, "
                         "VA3OSO, "
                         "VK1MIC, "
                         "W0FW, "
                         "W6BAZ,</strong><br/><br/>and the many other amateur radio operators who have helped<br/>"
                         "bring JS8Call into the world."}.arg(program_version()));
}

CAboutDlg::~CAboutDlg()
{
}
