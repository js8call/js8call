subroutine js8_downsample(dd,newdat,f0,c1)

  ! Downconvert to complex data sampled at 200 Hz ==> 32 samples/symbol

  !include 'js8_params.f90'

  parameter (NDFFT1=NSPS*NDD, NDFFT2=NDFFT1/NDOWN) ! Downconverted FFT Size - 192000/60 = 3200
  parameter (NTAPER=1) ! Should we taper the downsample?

  logical newdat,first

  complex c1(0:NDFFT2-1)
  complex cx(0:NDFFT1/2)
  real dd(NMAX),x(NDFFT1),taper(0:NDD)
  equivalence (x,cx)
  data first/.true./
  save cx,first,taper

  if(first) then
     pi=4.0*atan(1.0)
     do i=0,NDD
       taper(i)=0.5*(1.0+cos(i*pi/NDD))
     enddo
     first=.false.
  endif

  if(newdat) then
     if(NWRITELOG.eq.1) then
       write(*,*) '<DecodeDebug> newdat', NMAX, NDFFT1
       flush(6)
     endif

     ! Data in dd have changed, recompute the long FFT
     x(1:NMAX)=dd
     x(NMAX+1:NDFFT1)=0.                       !Zero-pad the x array
     call four2a(cx,NDFFT1,1,-1,0)             !r2c FFT to freq domain
     newdat=.false.
  endif

  df=12000.0/NDFFT1
  baud=12000.0/NSPS
  i0=nint(f0/df)
  ft=f0+8.5*baud
  it=min(nint(ft/df),NDFFT1/2)
  fb=f0-1.5*baud
  ib=max(1,nint(fb/df))
  k=0
  c1=0.

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> ds', df, baud, i0, ib, it
    flush(6)
  endif

  do i=ib,it
   c1(k)=cx(i)
   k=k+1
  enddo

  if(NTAPER.eq.1) then
    c1(0:NDD)=c1(0:NDD)*taper(NDD:0:-1)
    c1(k-1-NDD:k-1)=c1(k-1-NDD:k-1)*taper
  endif

  c1=cshift(c1,i0-ib)
  call four2a(c1,NDFFT2,1,1,1)            !c2c FFT back to time domain
  fac=1.0/sqrt(float(NDFFT1)*NDFFT2)
  c1=fac*c1

  return
end subroutine js8_downsample
