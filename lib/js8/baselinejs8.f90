subroutine baselinejs8(s,nfa,nfb,sbase)

! Fit baseline to spectrum (for JS8)
! Input:  s(npts)         Linear scale in power
! Output: sbase(npts)    Baseline

  implicit real*8 (a-h,o-z)

  !include 'js8_params.f90'
  
  real*4 s(NSPS)
  real*4 sbase(NSPS)
  real*4 base
  real*8 x(1000),y(1000),a(5)
  data nseg/10/,npct/10/

  df=12000.0/(NSPS*2.0d0)             !3.125 Hz
  ia=max(1,nint(nfa/df))
  ib=nint(nfb/df)
  do i=ia,ib
     s(i)=10.0*log10(s(i))            !Convert to dB scale
  enddo

  nterms=5
  nlen=(ib-ia+1)/nseg                 !Length of test segment
  i0=(ib-ia+1)/2                      !Midpoint
  k=0
  do n=1,nseg                         !Loop over all segments
     ja=ia + (n-1)*nlen
     jb=ja+nlen-1
     call pctile(s(ja),nlen,npct,base) !Find lowest npct of points
     do i=ja,jb
        if(s(i).le.base) then
           if (k.lt.1000) k=k+1       !Save all "lower envelope" points
           x(k)=i-i0
           y(k)=s(i)
        endif
     enddo
  enddo
  kz=k
  a=0.
  call polyfit(x,y,y,kz,nterms,0,a,chisqr)  !Fit a low-order polynomial
  sbase=0.0
  do i=ia,ib
     t=i-i0
     sbase(i)=a(1)+t*(a(2)+t*(a(3)+t*(a(4)+t*(a(5))))) + 0.65
!     write(51,3051) i*df,s(i),sbase(i)
!3051 format(3f12.3)
  enddo

  return
end subroutine baselinejs8
