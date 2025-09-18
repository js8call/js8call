#include <QDebug>
#include <QString>
#include <QSharedMemory>
#include <QSystemSemaphore>

#include "../commons.h"

// Multiple instances: KK1D, 17 Jul 2013
QSharedMemory mem_jt9;

// Semaphore not changed, as the acquire/release calls do not
// appear to be used anywhere.
QSystemSemaphore sem_jt9("sem_jt9", 1, QSystemSemaphore::Open);

extern "C" {
  bool attach_jt9_();
  bool create_jt9_(int nsize);
  bool detach_jt9_();
  bool lock_jt9_();
  bool unlock_jt9_();
  struct jt9com * address_jt9_();
  int size_jt9_();
// Multiple instances:  wrapper for QSharedMemory::setKey()
  bool setkey_jt9_(char* mykey, int mykey_len);

  bool acquire_jt9_();
  bool release_jt9_();
}

bool attach_jt9_() {return mem_jt9.attach();}
bool create_jt9_(int nsize) {return mem_jt9.create(nsize);}
bool detach_jt9_() {return mem_jt9.detach();}
bool lock_jt9_() {return mem_jt9.lock();}
bool unlock_jt9_() {return mem_jt9.unlock();}
struct jt9com * address_jt9_() {return reinterpret_cast<struct jt9com *>(mem_jt9.data());}
int size_jt9_() {return (int)mem_jt9.size();}

// Multiple instances:
bool setkey_jt9_(char* mykey, int mykey_len) {
   char *tempstr = (char *)calloc(mykey_len+1,1);
   memset(tempstr, 0, mykey_len+1);
   strncpy(tempstr, mykey, mykey_len);
   QString s1 = QString(QLatin1String(tempstr));
   mem_jt9.setKey(s1);
   return true;}

bool acquire_jt9_() {return sem_jt9.acquire();}
bool release_jt9_() {return sem_jt9.release();}
