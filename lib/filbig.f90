subroutine filbig(dd,npts,f0,newdat,c4a,n4,sq0)

! Filter and downsample the real data in array dd(npts), sampled at 12000 Hz.
! Output is complex, sampled at 1378.125 Hz.

  use, intrinsic :: iso_c_binding
  use FFTW3
  use timer_module, only: timer

  parameter (NSZ=3413)
  parameter (NFFT1=672000,NFFT2=77175,NH2=38587)
  parameter (NZ2=1000)
  real*4  dd(npts)                           !Input data
  real*4 rca(NFFT1)
  complex ca(NFFT1/2+1)                      !FFT of input
  complex c4a(NFFT2)                         !Output data
  real*4 s(NZ2)
  real*8 df
  real halfpulse(8)                 !Impulse response of filter (one sided)
  complex cfilt(NFFT2)                       !Filter (complex; imag = 0)
  real rfilt(NFFT2)                          !Filter (real)
  type(C_PTR) :: plan1,plan2,plan3           !Pointers to FFTW plans
  logical first
  equivalence (rfilt,cfilt),(rca,ca)
  data first/.true./
  data halfpulse/114.97547150,36.57879257,-20.93789101,              &
       5.89886379,1.59355187,-2.49138308,0.60910773,-0.04248129/
  common/refspec/dfref,ref(NSZ)
  common/patience/npatience,nthreads
  save first,plan1,plan2,plan3,rfilt,cfilt,df,ca

  if(npts.lt.0) go to 900                    !Clean up at end of program

  if(first) then
     nflags=FFTW_ESTIMATE
     if(npatience.eq.1) nflags=FFTW_ESTIMATE_PATIENT
     if(npatience.eq.2) nflags=FFTW_MEASURE
     if(npatience.eq.3) nflags=FFTW_PATIENT
     if(npatience.eq.4) nflags=FFTW_EXHAUSTIVE

! Plan the FFTs just once
     !$omp critical(fftw) ! serialize non thread-safe FFTW3 calls
     call fftwf_plan_with_nthreads(nthreads)
     plan1=fftwf_plan_dft_r2c_1d(nfft1,rca,ca,nflags)
     call fftwf_plan_with_nthreads(1)
     plan2=fftwf_plan_dft_1d(nfft2,c4a,c4a,-1,nflags)
     plan3=fftwf_plan_dft_1d(nfft2,cfilt,cfilt,+1,nflags)
     !$omp end critical(fftw)

! Convert impulse response to filter function
     do i=1,nfft2
        cfilt(i)=0.
     enddo
     fac=0.00625/nfft1
     cfilt(1)=fac*halfpulse(1)
     do i=2,8
        cfilt(i)=fac*halfpulse(i)
        cfilt(nfft2+2-i)=fac*halfpulse(i)
     enddo
     call fftwf_execute_dft(plan3,cfilt,cfilt)

     base=real(cfilt(NH2+1))
     do i=1,nfft2
        rfilt(i)=real(cfilt(i))-base
     enddo

     df=12000.d0/nfft1
     first=.false.
  endif

! When new data comes along, we need to compute a new "big FFT"
! If we just have a new f0, continue with the existing data in ca.

  if(newdat.ne.0) then
     call timer('FFTbig  ',0)
     nz=min(npts,nfft1)
     rca(1:nz)=dd(1:nz)
     rca(nz+1:)=0.
     call fftwf_execute_dft_r2c(plan1,rca,ca)
     call timer('FFTbig  ',1)

     ib=0
     do j=1,NSZ
        ia=ib+1
        ib=nint(j*dfref/df)
        fac=sqrt(min(30.0,1.0/ref(j)))
        ca(ia:ib)=fac*conjg(ca(ia:ib))
     enddo
     newdat=0
  endif

! NB: f0 is the frequency at which we want our filter centered.
!     i0 is the bin number in ca closest to f0.
  call timer('loops   ',0)
  i0=nint(f0/df) + 1
  do i=1,NH2                               !Copy data into c4a and apply
     j=i0+i-1                              !the filter function
     if(j.ge.1 .and. j.le.nfft1/2+1) then
        c4a(i)=rfilt(i)*ca(j)
     else
        c4a(i)=0.
     endif
  enddo
  do i=NH2+1,nfft2
     j=i0+i-1-nfft2
!     if(j.lt.1) j=j+nfft1                  !nfft1 was nfft2
     if(j.ge.1) then
        c4a(i)=rfilt(i)*ca(j)
     else
        c4a(i)=rfilt(i)*conjg(ca(2-j))
     endif
  enddo

  nadd=77                                   !nfft2/NZ2=77
  i=0
  do j=1,NZ2
     s(j)=0.
     do n=1,nadd
        i=i+1
        s(j)=s(j) + real(c4a(i))**2 + aimag(c4a(i))**2
     enddo
  enddo
  call pctile(s,NZ2,30,sq0)
  call timer('loops   ',1)

! Do the short reverse transform, to go back to time domain.
  call timer('FFTsmall',0)
  call fftwf_execute_dft(plan2,c4a,c4a)
  call timer('FFTsmall',1)
  n4=min(npts/8,nfft2)
  return

900 continue

  !$omp critical(fftw) ! serialize non thread-safe FFTW3 calls
  call fftwf_destroy_plan(plan1)
  call fftwf_destroy_plan(plan2)
  call fftwf_destroy_plan(plan3)
  !$omp end critical(fftw)
  
  return
end subroutine filbig
