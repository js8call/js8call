subroutine twkfreq(c3,c4,npts,fsample,a)

  complex c3(npts)
  complex c4(npts)
  complex w,wstep
  real a(3)
  data twopi/6.283185307/

! Mix the complex signal
  w=1.0
  wstep=1.0
  x0=0.5*(npts+1)
  s=2.0/npts
  do i=1,npts
     x=s*(i-x0)
     p2=1.5*x*x - 0.5
!          p3=2.5*(x**3) - 1.5*x
!          p4=4.375*(x**4) - 3.75*(x**2) + 0.375
     dphi=(a(1) + x*a(2) + p2*a(3)) * (twopi/fsample)
     wstep=cmplx(cos(dphi),sin(dphi))
     w=w*wstep
     c4(i)=w*c3(i)
  enddo

  return
end subroutine twkfreq
