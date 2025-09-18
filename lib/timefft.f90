program timefft

! Tests and times one-dimensional FFTs computed by FFTW3

  use, intrinsic :: iso_c_binding
  use FFTW3

  complex(C_FLOAT_COMPLEX),pointer :: a(:),b(:),c(:)
  real(C_FLOAT),pointer :: ar(:),br(:)
  type(C_PTR) :: plan1,plan2              !Pointers to FFTW plans
  type(C_PTR) :: pa,pb,pc
  integer(C_INT) iret
  integer*8 count0,count1,clkfreq
  character problem*9
  logical linplace,lcomplex,lthreading

! Get command-line parameters
  call timefft_opts(npatience,maxthreads,linplace,lcomplex,nfft,problem,nflags)
  lthreading=maxthreads.ge.1
  maxthreads=max(1,maxthreads)

  call sgran()                  ! see C rand generator (used in gran)

! Allocate data arrays
  pa=fftwf_alloc_complex(int(nfft,C_SIZE_T))
  call c_f_pointer(pa,a,[nfft])
  call c_f_pointer(pa,ar,[nfft])

  pb=fftwf_alloc_complex(int(nfft,C_SIZE_T))
  call c_f_pointer(pb,b,[nfft])
  call c_f_pointer(pb,br,[nfft])

  pc=fftwf_alloc_complex(int(nfft,C_SIZE_T))
  call c_f_pointer(pc,c,[nfft])

! Initialize FFTW threading
  if(lthreading) iret=fftwf_init_threads()

! Import FFTW wisdom, if available
  iret=fftwf_import_wisdom_from_filename(C_CHAR_'wis.dat' // C_NULL_CHAR)

  do i=1,nfft                           !Generate random data
     x=gran()
     y=gran()
     b(i)=cmplx(x,y)
  enddo
  iters=100

  write(*,1000)
1000 format(/'Problem  Threads Plan    Time    Gflops     RMS   iters'/    &
             '--------------------------------------------------------')

! Try nthreads = 1,maxthreads
  do nthreads=1,maxthreads
     a(1:nfft)=b(1:nfft)                             !Copy test data into a()
     call system_clock(count0,clkfreq)
! Make the plans
     if(lthreading) call fftwf_plan_with_nthreads(nthreads)
     if(lcomplex) then
        if(linplace) then
           plan1=fftwf_plan_dft_1d(nfft,a,a,-1,nflags)
           plan2=fftwf_plan_dft_1d(nfft,a,a,+1,nflags)
        else
           plan1=fftwf_plan_dft_1d(nfft,a,c,-1,nflags)
           plan2=fftwf_plan_dft_1d(nfft,c,a,+1,nflags)
        endif
     else
        if(linplace) then
           plan1=fftwf_plan_dft_r2c_1d(nfft,ar,a,nflags)
           plan2=fftwf_plan_dft_c2r_1d(nfft,a,ar,nflags)
        else
           plan1=fftwf_plan_dft_r2c_1d(nfft,ar,c,nflags)
           plan2=fftwf_plan_dft_c2r_1d(nfft,c,ar,nflags)
        endif
     endif
     call system_clock(count1,clkfreq)
     tplan=0.5*float(count1-count0)/float(clkfreq)    !Plan time for one transform

     total=0.
     do iter=1,iters                             !Do many iterations
        a=b                                      !Copy test data into a()
        call system_clock(count0,clkfreq)
! Compute the transforms
        if(lcomplex) then
           if(linplace) then
              call fftwf_execute_dft(plan1,a,a)
              call fftwf_execute_dft(plan2,a,a)
           else
              call fftwf_execute_dft(plan1,a,c)
              call fftwf_execute_dft(plan2,c,a)
           endif
        else
           if(linplace) then
              call fftwf_execute_dft_r2c(plan1,ar,a)
              call fftwf_execute_dft_c2r(plan2,a,ar)
           else
              call fftwf_execute_dft_r2c(plan1,ar,c)
              call fftwf_execute_dft_c2r(plan2,c,ar)
           endif
        endif
        call system_clock(count1,clkfreq)
        total=total + float(count1-count0)/float(clkfreq)
        if(total>=1.0 .and. iter>=10) go to 40     !Cut iterations short ?
     enddo
     iter=iters

40   time=0.5*total/iter                         !Time for one FFT
     gflops=5.0/(1.e9*time/(nfft*log(float(nfft))/log(2.0)))
     a(1:nfft)=a(1:nfft)/nfft              !Normalize the back-transformed data

! Compute RMS difference between original data and back-transformed data.
     sq=0.
     if(lcomplex) then
        do i=1,nfft
           sq=sq + real(a(i)-b(i))**2 + aimag(a(i)-b(i))**2
        enddo
     else
        do i=1,nfft
           sq=sq + (ar(i)-br(i))**2
        enddo
     endif
     rms=sqrt(sq/nfft)

! Display results
     write(*,1050) problem,nthreads,tplan,time,gflops,rms,iter
1050 format(a9,i4,f8.3,f10.6,f7.2,f11.7,i5)
  enddo

! Export accumulated FFTW wisdom
  iret=fftwf_export_wisdom_to_filename(C_CHAR_'wis.dat' // C_NULL_CHAR)

! Clean up
  call fftwf_destroy_plan(plan1)
  call fftwf_destroy_plan(plan2)
  call fftwf_free(pa)
  call fftwf_free(pb)
  call fftwf_free(pc)
  call fftwf_cleanup_threads()
  call fftwf_cleanup()

end program timefft
