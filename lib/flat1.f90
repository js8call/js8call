subroutine flat1(savg,iz,nsmo,syellow)

  real savg(iz)
  real syellow(iz)
  real x(8192)

  ia=nsmo/2 + 1
  ib=iz - nsmo/2 - 1
  nstep=20
  nh=nstep/2
  do i=ia,ib,nstep
     call pctile(savg(i-nsmo/2),nsmo,50,x(i))
     x(i-nh:i+nh-1)=x(i)
  enddo
  x(1:ia-1)=x(ia)
  x(ib+1:iz)=x(ib)

  x0=0.001*maxval(x(iz/10:(9*iz)/10))
  syellow(1:iz)=savg(1:iz)/(x(1:iz)+x0)

  return
end subroutine flat1

