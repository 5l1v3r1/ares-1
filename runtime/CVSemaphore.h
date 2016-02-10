/*
 * ###########################################################################
 *  Copyright 2015-2016. Los Alamos National Security, LLC. This software was
 *  produced under U.S. Government contract ??? (LA-CC-15-056) for Los
 *  Alamos National Laboratory (LANL), which is operated by Los Alamos
 *  National Security, LLC for the U.S. Department of Energy. The
 *  U.S. Government has rights to use, reproduce, and distribute this
 *  software.  NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL SECURITY,
 *  LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY
 *  FOR THE USE OF THIS SOFTWARE.  If software is modified to produce
 *  derivative works, such modified software should be clearly marked,
 *  so as not to confuse it with the version available from LANL.
 *
 *  Additionally, redistribution and use in source and binary forms,
 *  with or without modification, are permitted provided that the
 *  following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *    * Neither the name of Los Alamos National Security, LLC, Los
 *      Alamos National Laboratory, LANL, the U.S. Government, nor the
 *      names of its contributors may be used to endorse or promote
 *      products derived from this software without specific prior
 *      written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND
 *  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *  USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 *  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 * ###########################################################################
 *
 * Notes
 *
 * #####
 */

#ifndef __ARES_CV_SEMAPHORE_H__
#define __ARES_CV_SEMAPHORE_H__

#include <pthread.h>

namespace ares{

class CVSemaphore{
public:
  
  CVSemaphore(int count=0)
  : count_(count),
  maxCount_(0){

    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
  }
  
  CVSemaphore(int count, int maxCount)
  : count_(count),
  maxCount_(maxCount){

    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
  }
  
  ~CVSemaphore(){
    pthread_cond_destroy(&condition_);
    pthread_mutex_destroy(&mutex_);
  }

  bool acquire(double dt){
    timeval tv;
    gettimeofday(&tv, 0);
    
    double t = tv.tv_sec + tv.tv_usec/1e6;
    t += dt;
    
    pthread_mutex_lock(&mutex_);
          
    double sec = floor(t);
    double fsec = t - sec;

    timespec ts;
    ts.tv_sec = sec;
    ts.tv_nsec = fsec*1e9;

    while(count_ <= 0){
      if(pthread_cond_timedwait(&condition_, 
                                &mutex_,
                                &ts) != 0){
        pthread_mutex_unlock(&mutex_);
        return false;
      }
    }
    
    --count_;
    pthread_mutex_unlock(&mutex_);
    
    return true;
  }
  
  bool acquire(){
    pthread_mutex_lock(&mutex_);
    
    while(count_ <= 0){
      pthread_cond_wait(&condition_, &mutex_);
    }
    
    --count_;
    pthread_mutex_unlock(&mutex_);
    
    return true;
  }
  
  bool tryAcquire(){
    pthread_mutex_lock(&mutex_);
    
    if(count_ > 0){
      --count_;
      pthread_mutex_unlock(&mutex_);
      return true;
    }

    pthread_mutex_unlock(&mutex_);
    return false;
  }
  
  void release(){
    pthread_mutex_lock(&mutex_);

    if(maxCount_ == 0 || count_ < maxCount_){
      ++count_;
    }

    pthread_cond_signal(&condition_);
    pthread_mutex_unlock(&mutex_);
  }
  
  CVSemaphore& operator=(const CVSemaphore&) = delete;
  
  CVSemaphore(const CVSemaphore&) = delete;

private:
  pthread_mutex_t mutex_;
  pthread_cond_t condition_;

  int count_;
  int maxCount_;
};

} // namespace ares

 #endif // __ARES_CV_SEMAPHORE_H__
