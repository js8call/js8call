subroutine syncjs8d(cd0,icos,i0,delf,sync)
! Compute sync power for a complex, downsampled JS8 signal.

  !include 'js8_params.f90'
  parameter(NP=NMAX/NDOWN, NP2=NN*NDOWNSPS)
  complex cd0(0:NP-1)
  complex csynca(0:6,NDOWNSPS),csyncb(0:6,NDOWNSPS),csyncc(0:6,NDOWNSPS)
  complex csync2a(NDOWNSPS),csync2b(NDOWNSPS),csync2c(NDOWNSPS)
  complex ctwk(NDOWNSPS)
  complex z1,z2,z3
  real delf
  logical first
  integer icos
  data first/.true./
  save first,twopi,csynca,csyncb,csyncc

  p(z1)=(real(z1)**2 + aimag(z1)**2)          !Statement function for power

  integer icos7a(0:6), icos7b(0:6), icos7c(0:6)

  if(icos.eq.1) then
    icos7a = (/4,2,5,6,1,3,0/)                  !Beginning Costas 7x7 tone pattern
    icos7b = (/4,2,5,6,1,3,0/)                  !Middle Costas 7x7 tone pattern
    icos7c = (/4,2,5,6,1,3,0/)                  !End Costas 7x7 tone pattern
  else
    icos7a = (/0,6,2,3,5,4,1/)                  !Beginning Costas 7x7 tone pattern
    icos7b = (/1,5,0,2,3,6,4/)                  !Middle Costas 7x7 tone pattern
    icos7c = (/2,5,0,6,4,1,3/)                  !End Costas 7x7 tone pattern
  endif

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> syncjs8d costas', icos7a, icos7b, icos7c
    flush(6)
  endif

  ! Set some constants and compute the csync array.  
  if( first ) then
    twopi=8.0*atan(1.0)

    do i=0,6
      phia=0.0
      phib=0.0
      phic=0.0
      dphia=twopi*icos7a(i)/NDOWNSPS
      dphib=twopi*icos7b(i)/NDOWNSPS
      dphic=twopi*icos7c(i)/NDOWNSPS

      do j=1,NDOWNSPS
        csynca(i,j)=cmplx(cos(phia),sin(phia)) !Waveform for Beginning 7x7 Costas array
        csyncb(i,j)=cmplx(cos(phib),sin(phib)) !Waveform for Middle 7x7 Costas array
        csyncc(i,j)=cmplx(cos(phic),sin(phic)) !Waveform for End 7x7 Costas array
        phia=mod(phia+dphia,twopi)
        phib=mod(phib+dphib,twopi)
        phic=mod(phia+dphic,twopi)

        if(NWRITELOG.eq.1) then
            write(*,*) '<DecodeDebug> computing costas waveforms', i, j, phia, phib, phic, dphia, dphib, dphic
            flush(6)
        endif
      enddo
    
    enddo
    first=.false.
  endif

  ! compute the ctwk samples at the delta frequency to by used to detect peaks
  ! at frequencies +/- delf so we can align the decoder for the best possible
  ! chance at decoding.
  !
  ! NOTE: this does not need to compute the entire set of samples for the 
  !       costas arrays...it only needs to do it for the delta frequency
  !       whose conjugate is multiplied against each csync arrays for those 
  !       coastas signals
  if(delf.ne.0.0) then
    fs2=12000.0/NDOWN
    dt2=1.0/fs2
    dphi=twopi*delf*dt2
    phi=0.0
    do i=1,NDOWNSPS
      ctwk(i)=cmplx(cos(phi),sin(phi))
      phi=mod(phi+dphi,twopi)
    enddo
  endif

  sync=0
  do i=0,6
    i1=i0+i*NDOWNSPS
    i2=i1+36*NDOWNSPS
    i3=i1+72*NDOWNSPS

    z1=0.
    z2=0.
    z3=0.

    csync2a=csynca(i,1:NDOWNSPS)
    if(delf.ne.0.0) csync2a=ctwk*csync2a
    if(i1.ge.0 .and. i1+NDOWNSPS-1.le.NP2-1) z1=sum(cd0(i1:i1+NDOWNSPS-1)*conjg(csync2a))

    csync2b=csyncb(i,1:NDOWNSPS)
    if(delf.ne.0.0) csync2b=ctwk*csync2b
    if(i2.ge.0 .and. i2+NDOWNSPS-1.le.NP2-1) z2=sum(cd0(i2:i2+NDOWNSPS-1)*conjg(csync2b))

    csync2c=csyncc(i,1:NDOWNSPS)
    if(delf.ne.0.0) csync2c=ctwk*csync2c
    if(i3.ge.0 .and. i3+NDOWNSPS-1.le.NP2-1) z3=sum(cd0(i3:i3+NDOWNSPS-1)*conjg(csync2c))

    sync = sync + p(z1) + p(z2) + p(z3)

    if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug> sync computation', i, sync
        flush(6)
    endif
  enddo

  return
end subroutine syncjs8d
