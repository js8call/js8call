subroutine syncjs8(dd,icos,nfa,nfb,syncmin,nfqso,s,candidate,ncand,sbase)

  !include 'js8_params.f90'
  
  complex cx(0:NH1)
  real s(NH1,NHSYM)
  real savg(NH1)
  real sbase(NH1)
  real x(NFFT1)
  real sync2d(NH1,-JZ:JZ)
  real red(NH1)
  real candidate0(3,NMAXCAND)
  real candidate(3,NMAXCAND)
  real dd(NMAX)
  integer icos
  integer jpeak(NH1)
  integer indx(NH1)
  integer ii(1)
  integer syoff !symbol offset
  equivalence (x,cx)

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
    write(*,*) '<DecodeDebug> syncjs8 costas', icos7a, icos7b, icos7c
    flush(6)
  endif

! Compute symbol spectra, stepping by NSTEP steps.  
  savg=0.
  tstep=NSTEP/12000.0                         
  df=12000.0/NFFT1                  
  fac=1.0/300.0
  do j=1,NHSYM
     ia=(j-1)*NSTEP + 1
     ib=ia+NSPS-1
     x(1:NSPS)=fac*dd(ia:ib)
     x(NSPS+1:)=0.
     call four2a(cx,NFFT1,1,-1,0)              !r2c FFT
     do i=1,NH1
        s(i,j)=real(cx(i))**2 + aimag(cx(i))**2
     enddo
     savg=savg + s(1:NH1,j)                   !Average spectrum
  enddo

  call baselinejs8(savg,nfa,nfb,sbase)

  ia=max(1,nint(nfa/df)) ! min freq
  ib=nint(nfb/df)        ! max freq
  nssy=NSPS/NSTEP        ! steps per symbol
  nfos=NFFT1/NSPS        ! frequency bin oversampling factor
  jstrt=ASTART/tstep     ! the symbol step that we are starting at (NHSYM)

  candidate0=0.
  k=0

  do i=ia,ib
     do j=-JZ,+JZ
        ta=0.
        tb=0.
        tc=0.
        t0a=0.
        t0b=0.
        t0c=0.
        do n=0,6
           k=j+jstrt+nssy*n
           
           syoff=k
           if(syoff.ge.1.and.syoff.le.NHSYM) then
              ta=ta + s(i+nfos*icos7a(n),syoff)
              t0a=t0a + sum(s(i:i+nfos*6:nfos,syoff))
           endif

           syoff=k+nssy*36
           if(syoff.ge.1.and.syoff.le.NHSYM) then
              tb=tb + s(i+nfos*icos7b(n),syoff)
              t0b=t0b + sum(s(i:i+nfos*6:nfos,syoff))
           endif
           
           syoff=k+nssy*72
           if(syoff.ge.1.and.syoff.le.NHSYM) then
              tc=tc + s(i+nfos*icos7c(n),syoff)
              t0c=t0c + sum(s(i:i+nfos*6:nfos,syoff))
           endif
        enddo

        t=ta+tb+tc
        t0=t0a+t0b+t0c
        t0=(t0-t)/6.0
        sync_abc=t/t0

        t=ta+tb
        t0=t0a+t0b
        t0=(t0-t)/6.0
        sync_ab=t/t0

        t=ta+tc
        t0=t0a+t0c
        t0=(t0-t)/6.0
        sync_ac=t/t0

        t=tb+tc
        t0=t0b+t0c
        t0=(t0-t)/6.0
        sync_bc=t/t0

        !sync2d(i,j)=max(max(max(sync_abc, sync_ab), sync_ac), sync_bc)
        sync2d(i,j)=max(sync_abc, sync_ab, sync_bc)
     enddo
  enddo

  red=0.
  do i=ia,ib
     ii=maxloc(sync2d(i,-JZ:JZ)) - 1 - JZ
     j0=ii(1)
     jpeak(i)=j0
     red(i)=sync2d(i,j0)
  enddo

  iz=ib-ia+1
  call indexx(red(ia:ib),iz,indx)

  npctile=nint(0.40*iz)
  if(npctile.lt.1) then ! bail
      ncand=0
      return;
  endif
  
  ibase=indx(npctile) - 1 + ia
  if(ibase.lt.1) ibase=1
  if(ibase.gt.nh1) ibase=nh1
  base=red(ibase)
  red=red/base

  k=0
  do i=1,min(iz,NMAXCAND)
    if(k.ge.NMAXCAND) exit

    n=ia + indx(iz+1-i) - 1

    if(red(n).lt.syncmin.or.isnan(red(n))) exit
    
    if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug> red candidate', red(n), n*df, (jpeak(n)-1)*tstep
        flush(6)
    endif
    
    k=k+1
    
    candidate0(1,k)=n*df
    candidate0(2,k)=(jpeak(n)-0.5)*tstep
    candidate0(3,k)=red(n)
  enddo
  ncand=k

  ! Save only the best of near-dupe freqs.  
  do i=1,ncand
     if(i.ge.2) then
        do j=1,i-1
           fdiff=abs(candidate0(1,i))-abs(candidate0(1,j))
           if(abs(fdiff).lt.AZ) then                                     ! note: this dedupe difference is dependent on symbol spacing
              if(NWRITELOG.eq.1) then
                  write(*,*) '<DecodeDebug> dupe', fdiff, candidate0(1,i), candidate0(3,i), candidate0(1,j), candidate0(3,j)
                  flush(6)
              endif
              if(candidate0(3,i).ge.candidate0(3,j)) candidate0(3,j)=0.
              if(candidate0(3,i).lt.candidate0(3,j)) candidate0(3,i)=0.
           endif
        enddo
     endif
  enddo

  ! Put nfqso at top of list
  do i=1,ncand
     if(abs(candidate0(1,i)-nfqso).lt.10.0) candidate0(1,i)=-candidate0(1,i)
  enddo

  fac=20.0/maxval(s)
  s=fac*s

  ! Sort by sync
  ! call indexx(candidate0(3,1:ncand),ncand,indx)
  
  ! Sort by frequency 
  call indexx(candidate0(1,1:ncand),ncand,indx)

  k=1
  do i=1,ncand
     j=indx(i)
     if( candidate0(3,j) .ge. syncmin ) then
       candidate(2:3,k)=candidate0(2:3,j)
       candidate(1,k)=abs(candidate0(1,j))
       k=k+1
     endif
  enddo
  ncand=k-1
  return
end subroutine syncjs8
