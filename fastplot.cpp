#include "fastplot.h"
#include "commons.h"
#include <math.h>
#include <QDebug>

#include "DriftingDateTime.h"

#include "moc_fastplot.cpp"

#define MAX_SCREENSIZE 2048

FPlotter::FPlotter(QWidget *parent) :                  //FPlotter Constructor
  QFrame {parent},
  m_w {703},
  m_plotGain {0},
  m_greenZero {0},
  m_x0 {0},
  m_x1 {0},
  m_ScalePixmap {QPixmap {703, 20}},
  m_pixPerSecond {12000.0/512.0},
  m_hdivs {30},
  m_h {220},
  m_h1 {20},
  m_h2 {m_h-m_h1},
  m_HorizPixmap {QPixmap {m_w, m_h2}},
  m_jh0 {9999},
  m_bPaint2 {true}
{
  m_diskData=false;
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_PaintOnScreen,false);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);

  m_HorizPixmap.fill(Qt::black);
  m_HorizPixmap.fill(Qt::black);
  m_ScalePixmap.fill(Qt::white);
  drawScale();
  draw();
  setMouseTracking(true);
}

FPlotter::~FPlotter() { }                                      // Destructor

void FPlotter::paintEvent(QPaintEvent *)                       // paintEvent()
{
  QPainter painter(this);
  painter.drawPixmap(0,0,m_ScalePixmap);
  painter.drawPixmap(0,m_h1,m_HorizPixmap);
}

void FPlotter::drawScale()                                 //drawScale()
{
  if(m_ScalePixmap.isNull()) return;
  int x;

  QRect rect0;
  QPainter painter0(&m_ScalePixmap);
  painter0.initFrom(this);

  //create Font to use for scales
  QFont Font("Arial");
  Font.setPointSize(8);
  QFontMetrics metrics(Font);
  Font.setWeight(QFont::Normal);
  painter0.setFont(Font);
  painter0.setPen(Qt::white);
  m_ScalePixmap.fill(Qt::black);
  painter0.drawRect(0, 0,m_w,19);
  painter0.drawLine(0,19,m_w,19);

//Draw ticks at 1-second intervals
  for( int i=0; i<=m_hdivs; i++) {
    x = (int)( (float)i*m_pixPerSecond );
    painter0.drawLine(x,15,x,19);
  }

//Write numbers on the time scale
  MakeTimeStrs();
  for( int i=0; i<=m_hdivs; i++) {
    if(0==i) {
      //left justify the leftmost text
      x = (int)( (float)i*m_pixPerSecond);
      rect0.setRect(x,0, (int)m_pixPerSecond, 20);
      painter0.drawText(rect0, Qt::AlignLeft|Qt::AlignVCenter, m_HDivText[i]);
    }
    else if(m_hdivs == i) {
      //right justify the rightmost text
      x = (int)( (float)i*m_pixPerSecond - m_pixPerSecond);
      rect0.setRect(x,0, (int)m_pixPerSecond, 20);
      painter0.drawText(rect0, Qt::AlignRight|Qt::AlignVCenter, m_HDivText[i]);
    } else {
      //center justify the rest of the text
      x = (int)( (float)i*m_pixPerSecond - m_pixPerSecond/2);
      rect0.setRect(x,0, (int)m_pixPerSecond, 20);
      painter0.drawText(rect0, Qt::AlignCenter, m_HDivText[i]);
    }
  }
}

void FPlotter::MakeTimeStrs()                              //MakeTimeStrs
{
  for(int i=0; i<=m_hdivs; i++) {
    m_HDivText[i].setNum(i);
  }
}

int FPlotter::XfromTime(float t)                               //XfromFreq()
{
  return int(t*m_pixPerSecond);
}

float FPlotter::TimefromX(int x)                               //FreqfromX()
{
  return float(x/m_pixPerSecond);
}

void FPlotter::setPlotZero(int plotZero)                  //setPlotZero()
{
  m_plotZero=plotZero;
  m_bPaint2=true;
}

void FPlotter::setPlotGain(int plotGain)                  //setPlotGain()
{
  m_plotGain=plotGain;
  m_bPaint2=true;
}

void FPlotter::setGreenZero(int n)
{
  m_greenZero=n;
  m_bPaint2=true;
}

void FPlotter::setTRperiod(int n)
{
  m_TRperiod=n;
  m_pixPerSecond=12000.0/512.0;
  if(m_TRperiod<30) m_pixPerSecond=12000.0/256.0;
  drawScale();
  update();
}


void FPlotter::draw()                                         //draw()
{
  QPainter painter1(&m_HorizPixmap);
  QPoint LineBuf[703];
  QPen penGreen(Qt::green,1);

  if(m_diskData) {
    int ih=m_UTCdisk/10000;
    int im=m_UTCdisk/100 % 100;
    int is=m_UTCdisk % 100;
    m_t.sprintf("%2.2d:%2.2d:%2.2d",ih,im,is);
  }

  int k0=m_jh0;
  if(fast_jh < m_jh0 or m_bPaint2) {
    k0=0;
    QRect tmp(0,0,m_w,119);
    painter1.fillRect(tmp,Qt::black);
    painter1.setPen(Qt::white);
    if(m_diskData) {
      int ih=m_UTCdisk/10000;
      int im=m_UTCdisk/100 % 100;
      int is=m_UTCdisk % 100;
      m_t.sprintf("%2.2d:%2.2d:%2.2d",ih,im,is);
    } else {
      m_t=DriftingDateTime::currentDateTimeUtc().toString("hh:mm:ss");
    }
    if(fast_jh>0) painter1.drawText(10,95,m_t);
  }

  float gain = pow(10.0,(m_plotGain/20.0));
  for(int k=64*k0; k<64*fast_jh; k++) {                     //Upper spectrogram
    int i = k%64;
    int j = k/64;
    int y=0.005*gain*fast_s[k] + m_plotZero;
    if(y<0) y=0;
    if(y>254) y=254;
    painter1.setPen(g_ColorTbl[y]);
    painter1.drawPoint(j,64-i);
  }

  painter1.setPen(penGreen);                                // Upper green curve
  int j=0;
  m_greenGain=10;
  float greenGain = pow(10.0,(m_greenGain/20.0));
  for(int x=k0; x<=fast_jh; x++) {
    int y = 0.9*m_h - greenGain*fast_green[x] - m_greenZero + 40;
    if(y>119) y=119;
    LineBuf[j].setX(x);
//    LineBuf[j].setX(2*x);
    LineBuf[j].setY(y);
    j++;
  }
  painter1.drawPolyline(LineBuf,j);

  if((fast_jh < m_jh0) or m_bPaint2) {
    QRect tmp(0,120,m_w,219);
    painter1.fillRect(tmp,Qt::black);
    painter1.setPen(Qt::white);
    if(fast_jh>0 and m_jh0 < 9999) painter1.drawText(10,195,m_t0);
    m_t0=m_t;

    for(int k=0; k<64*fast_jh2; k++) {                      //Lower spectrogram
      int i = k%64;
      int j = k/64;
      int y=0.005*gain*fast_s2[k] + m_plotZero;
      if(y<0) y=0;
      if(y>254) y=254;
      painter1.setPen(g_ColorTbl[y]);
      painter1.drawPoint(j,164-i);
    }

    painter1.setPen(penGreen);                              //Lower green curve
    j=0;
    for(int x=0; x<=fast_jh2; x++) {
      int y = 0.9*m_h - greenGain*fast_green2[x] - m_greenZero + 140;
      if(y>219) y=219;
      LineBuf[j].setX(x);
      LineBuf[j].setY(y);
      j++;
    }
    painter1.drawPolyline(LineBuf,j);
    m_bPaint2=false;
  }

  painter1.setPen(Qt::white);
  painter1.drawLine(0,100, m_w,100);
  if(fast_jh>0) m_jh0=fast_jh;
  update();                                             //trigger a new paintEvent
}

void FPlotter::mouseMoveEvent(QMouseEvent *event)
{
  QPainter painter(&m_HorizPixmap);
  int x=event->x();
//  int y=event->y();
  float t=x/m_pixPerSecond;
  QString t1;
  t1.sprintf("%5.2f",t);
  QRectF rect0(78,85,40,13);              //### Should use font metrics ###
  painter.fillRect(rect0,Qt::black);
  painter.setPen(Qt::yellow);
  painter.drawText(80,95,t1);
  update();
}

void FPlotter::mousePressEvent(QMouseEvent *event)      //mousePressEvent
{
  if(m_mode=="MSK144") return;
  int x=event->x();
  int y=event->y();
  int n=event->button();
//  bool ctrl = (event->modifiers() & Qt::ControlModifier);
  QPainter painter(&m_HorizPixmap);
  int x0=x-n*m_pixPerSecond;
  int x1=x+n*m_pixPerSecond;
  int xmax=m_TRperiod*m_pixPerSecond;
  if(x0 < 0) x0=0;
  if(x1 > xmax) x1=xmax;
  if(x1 > 702) x1=702;
  Q_EMIT fastPick (x0,x1,y);
  int y0=64;
  if(y >= 120) y0+=100;
  if(m_x0+m_x1 != 0) {
    painter.setPen(Qt::black);
    painter.drawLine(m_x0,m_y0,m_x1,m_y0);              //Erase previous yellow line
    painter.drawLine(m_x0,m_y0-3,m_x0,m_y0+3);
    painter.drawLine(m_x1,m_y0-3,m_x1,m_y0+3);
  }
  painter.setPen(Qt::yellow);
  painter.drawLine(x0,y0,x1,y0);                        //Draw yellow line
  painter.drawLine(x0,y0-3,x0,y0+3);
  painter.drawLine(x1,y0-3,x1,y0+3);
  update();                                             //trigger a new paintEvent
  m_x0=x0;
  m_x1=x1;
  m_y0=y0;
}

void FPlotter::setMode(QString mode)                            //setMode
{
  m_mode=mode;
}
