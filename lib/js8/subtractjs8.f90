subroutine subtractjs8(dd,itone,f0,dt)
! Subtract an ft8 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  parameter (NSHIFT=0)
  parameter (NFRAME=NSPS*NN)
  parameter (NFFT=NMAX, NFILT=1400)

  real*4 dd(NMAX), window(-NFILT/2:NFILT/2)
  !complex cref,camp,cfilt,cw
  complex, allocatable :: cref(:),camp(:),cfilt(:),cw(:)
  integer itone(NN)
  logical first
  data first/.true./
  !common/heap8/cref(NFRAME),camp(NMAX),cfilt(NMAX),cw(NMAX)
  save first

  allocate(cref(NFRAME))
  allocate(camp(NMAX))
  allocate(cfilt(NMAX))
  allocate(cw(NMAX))

  nstart=dt*12000+1

  if(NWRITELOG.eq.1) then
      write(*,*) '<DecodeDebug> generating reference signal', nstart
      flush(6)
  endif

  call genjs8refsig(itone,cref,f0)

  camp=0.
  do i=1,NFRAME
    id=nstart-1+i 
    if(id.ge.1.and.id.le.NMAX) camp(i)=dd(id)*conjg(cref(i))
  enddo

  if(NWRITELOG.eq.1) then
      write(*,*) '<DecodeDebug> filtering', NFFT
      flush(6)
  endif

  if(first) then
      ! Create and normalize the filter
      if(NWRITELOG.eq.1) then
          write(*,*) '<DecodeDebug> creating and normalizing filter'
          flush(6)
      endif

      if(NSHIFT.eq.0) then
        pi=4.0*atan(1.0)
        fac=1.0/float(NFFT)
        sum=0.0
        do j=-NFILT/2,NFILT/2
            window(j)=cos(pi*j/NFILT)**2
            sum=sum+window(j)
        enddo
        cw=0.
        window=window/sum

        ! windows struggles with this cshift...i'm not sure why!?
        !cw(1:NFILT+1)=window
        !cw=cshift(cw,NFILT/2+1)
        ! so, this should be the same computation result
        cw(1:NFILT/2)=window(1:NFILT/2)
        cw(NFILT/2:NFILT+1)=0
        cw(NMAX-NFILT/2:NMAX)=window(-NFILT/2:0)

        call four2a(cw,NFFT,1,-1,1)
        cw=cw*fac
        first=.false.
      else
        pi=4.0*atan(1.0)
        fac=1.0/float(NFFT)
        sum=0.0
        do j=-NFILT/2,NFILT/2
           window(j)=cos(pi*j/NFILT)**2
           sum=sum+window(j)
        enddo
        cw=0.
        ! this ultimately shifts 1/2 of the window out of computation
        ! since it's multiplied against cfilt whiich will only have amp
        ! values for NFRAME length, which will always be > 20000 samples
        ! longer than the NFRAME.
        ! cw(1:NFILT+1)=window/sum
        ! cw=cshift(cw,NFILT/2+1)
        cw(1:NFILT/2)=window(1:NFILT/2)
        ! we really don't even need the second half of the window.
        ! start=NMAX-NFILT/2
        ! end=NMAX-NFILT+1
        ! cw(start:end)=window(-NFILT/2:1)
        cw=cw/sum
        call four2a(cw,NFFT,1,-1,1)
        cw=cw*fac
        first=.false.
      endif
  endif

  if(NWRITELOG.eq.1) then
      write(*,*) '<DecodeDebug> generating complex amplitude'
      flush(6)
  endif

  cfilt=0.0
  cfilt(1:NFRAME)=camp(1:NFRAME)
  call four2a(cfilt,NFFT,1,-1,1)
  cfilt(1:NFFT)=cfilt(1:NFFT)*cw(1:NFFT)
  call four2a(cfilt,NFFT,1,1,1)

  if(NWRITELOG.eq.1) then
      write(*,*) '<DecodeDebug> subtracting filtered reference', NFFT
      flush(6)
  endif

! Subtract the reconstructed signal
  do i=1,NFRAME
     j=nstart+i-1
     if(j.ge.1 .and. j.le.NMAX) dd(j)=dd(j)-2*REAL(cfilt(i)*cref(i))
  enddo

  return
end subroutine subtractjs8
