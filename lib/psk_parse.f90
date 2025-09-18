program psk_parse

  character line*256,callsign*12,callsign0*12,progname*30
  integer nc(6),ntot(6),nsingle(6)
  logical zap
  data callsign0/'            '/

  open(10,file='jt65-2',status='old')
  nc=0
  ntot=0
  ncalls=0
  nsingle=0
  zap=.false.

  do iline=1,9999999
     read(10,'(a256)',end=900) line
     n=len(trim(line))
     i1=0
     i2=0
     i3=0
     do i=1,n
        if(ichar(line(i:i)).eq.9) then
           if(i1.eq.0) then
              i1=i
              cycle
           endif
           if(i2.eq.0) then
              i2=i
              cycle
           endif
           if(i3.eq.0) then
              i3=i
              exit
           endif
        endif
     enddo
     callsign=line(1:i1-1)

     if(zap) then
        if(callsign(1:1).ge.'0' .and. callsign(1:1).le.'9' .and.               &
           callsign(2:2).ge.'0' .and. callsign(2:2).le.'9' .and.               &
           callsign(3:3).ge.'0' .and. callsign(3:3).le.'9') cycle

        if(callsign(1:1).ge.'A' .and. callsign(1:1).le.'Z' .and.               &
           callsign(2:2).ge.'A' .and. callsign(2:2).le.'Z' .and.               &
           callsign(3:3).ge.'A' .and. callsign(3:3).le.'Z' .and.               &
           callsign(4:4).ge.'A' .and. callsign(4:4).le.'Z') cycle

        if(callsign(1:1).eq.'N' .and.                                        &
           callsign(2:2).ge.'0' .and. callsign(2:2).le.'9' .and.               &
           callsign(3:3).ge.'0' .and. callsign(3:3).le.'9') cycle

     endif

     progname=line(i1+1:i2-1)
     do i=1,len(trim(progname))
        if(progname(i:i).eq.' ') progname(i:i)='_'
     enddo
     read(line(i2+1:i3-1),*) ndecodes
     read(line(i3+1:),*) nreporters

     j=6
     if(index(progname,'WSJT-X').gt.0) j=1
     if(index(progname,'HB9HQX').gt.0) j=2
     if(index(progname,'JTDX').gt.0) j=3
     if(index(progname,'Comfort').gt.0) j=4
     if(index(progname,'COMFORT').gt.0) j=4
     if(index(progname,'JT65-HF').gt.0 .and. j.eq.6) j=5

     nctot=sum(nc)
     if(callsign.ne.callsign0 .and. nctot.gt.0) then
        write(13,1000) callsign0,nc,nctot
1000    format(a12,6i8,i10)
        if(nctot.eq.1) nsingle(j)=nsingle(j)+1
        nc=0
        callsign0=callsign
        ncalls=ncalls+1
     endif
     nc(j)=nc(j) + ndecodes
     ntot(j)=ntot(j) + ndecodes
     write(12,1010) iline,callsign,ndecodes,nreporters,progname
1010 format(i8,2x,a12,2x,2i8,2x,a30)
  enddo

900 nctot=sum(nc)
  if(nctot.gt.0) write(13,1000) callsign0,nc,nctot

  write(*,1018)
1018 format('   Total    WSJT-X  HB9HQX   JTDX  Comfort JT65-HF   Other'/  &
            '----------------------------------------------------------')
  write(*,1019)
1019 format('Spots reported; fraction of total:')
  write(*,1020) sum(ntot),ntot
1020 format(i8,2x,6i8)
  fac=1.0/sum(ntot)
  write(*,1030) 1.0000,fac*ntot
1030 format(f8.4,2x,6f8.4,f10.4)

  write(*,1038)
1038 format(/'Singletons; as fraction of spots by same program')
  write(*,1040) sum(nsingle),nsingle
1040 format(i8,2x,6i8)
  write(*,1030) float(sum(nsingle))/sum(ntot),float(nsingle)/ntot

  write(*,1050) ncalls
1050 format(/'Distinct calls spotted:',i6)

end program psk_parse
