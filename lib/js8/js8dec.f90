subroutine js8dec(dd0,icos,newdat,syncStats,nfqso,ndepth, &
     napwid,lsubtract,nagain,iaptype,f1,xdt,xbase,apsym,  &
     nharderrors,dmin,nbadcrc,ipass,msg37,xsnr)  

  use crc
  use timer_module, only: timer

  !include 'js8_params.f90'
  
  parameter(NP=3200) ! why 3200? not sure...
  parameter(NP2=2812)
  character*37 msg37
  character message*22,msgsent*22,origmsg*22
  logical syncStats
  real a(5)
  real s1(0:7,ND),s2(0:7,NN),s1sort(8*ND)
  real ps(0:7),psl(0:7)
  real bmeta(3*ND),bmetb(3*ND),bmetap(3*ND)
  real llr(3*ND),llra(3*ND),llr0(3*ND),llr1(3*ND),llrap(3*ND)           !Soft symbols
  real dd0(NMAX)
  integer icos
  integer*1 decoded(KK),decoded0(KK),cw(3*ND)
  integer*1 msgbits(KK)
  integer apsym(KK)
  integer mcq(28),mde(28),mrrr(16),m73(16),mrr73(16)
  integer itone(NN)
  integer indxs1(8*ND)
  integer ip(1)
  integer naptypes(0:5,4) ! (decoding pass)  maximum of 4 passes for now
  complex cd0(0:NP-1)
  complex csymb(NDOWNSPS)
  logical first,newdat,lsubtract,nagain
  equivalence (s1,s1sort)
  data mcq/1,1,1,1,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,1,1,0,0,1/
  data mrrr/0,1,1,1,1,1,1,0,1,1,0,0,1,1,1,1/
  data m73/0,1,1,1,1,1,1,0,1,1,0,1,0,0,0,0/
  data mde/1,1,1,1,1,1,1,1,0,1,1,0,0,1,0,0,0,0,0,1,1,1,0,1,0,0,0,1/
  data mrr73/0,0,0,0,0,0,1,0,0,0,0,1,0,1,0,1/
  data first/.true./
  save naptypes

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
    write(*,*) '<DecodeDebug> js8dec costas', icos7a, icos7b, icos7c
    flush(6)
  endif

  if(first) then
     mcq=2*mcq-1
     mde=2*mde-1
     mrrr=2*mrrr-1
     m73=2*m73-1
     mrr73=2*mrr73-1

     ! iaptype
     !------------------------
     !   1        CQ     ???    ???
     !   2        MyCall ???    ???
     !   3        MyCall DxCall ???
     !   4        MyCall DxCall RRR
     !   5        MyCall DxCall 73
     !   6        MyCall DxCall RR73
     !   7        ???    DxCall ???

     naptypes(0,1:4)=(/1,2,0,0/)
     naptypes(1,1:4)=(/2,3,0,0/)
     naptypes(2,1:4)=(/2,3,0,0/)
     naptypes(3,1:4)=(/3,4,5,6/)
     naptypes(4,1:4)=(/3,4,5,6/)
     naptypes(5,1:4)=(/3,1,2,0/)  
     first=.false.
  endif

  max_iterations=30
  nharderrors=-1
  fs2=12000.0/NDOWN
  dt2=1.0/fs2
  twopi=8.0*atan(1.0)
  delfbest=0.
  ibest=0

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> downsampling', f1, fs2, dt2
    flush(6)
  endif

  call timer('js8_down',0)
  call js8_downsample(dd0,newdat,f1,cd0)   !Mix f1 to baseband and downsample
  call timer('js8_down',1)

  i0=nint((xdt+ASTART)*fs2)                   !Initial guess for start of signal
  smax=0.0

  if(NWRITELOG.eq.1) then
      write(*,*) '<DecodeDebug> initial dt guess', i0, xdt
      flush(6)
  endif

  do idt=i0-NQSYMBOL,i0+NQSYMBOL             !Search over +/- one quarter symbol
     call syncjs8d(cd0,icos,idt,0.0,sync)
  
     if(NWRITELOG.eq.1) then
         write(*,*) '<DecodeDebug> idt', idt, 'sync', sync
         flush(6)
     endif
     
     if(sync.gt.smax) then
        smax=sync
        ibest=idt
     endif
  enddo

  xdt2=ibest*dt2                           !Improved estimate for DT

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> xdt2', xdt2, 'ibest', ibest
    flush(6)
  endif

  ! Now peak up in frequency
  i0=nint(xdt2*fs2)
  smax=0.0

  ! Search over +/- 1/2*nfsrch Hz (i.e., +/- 2.5Hz)
  do ifr=-NFSRCH,NFSRCH
    delf=ifr*0.5

    call syncjs8d(cd0,icos,i0,delf,sync)

    if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug> df', delf, 'sync', sync
        flush(6)
    endif

    if( sync .gt. smax ) then
      smax=sync
      delfbest=delf
    endif
  enddo

  a=0.0
  a(1)=-delfbest

  call twkfreq1(cd0,NP2,fs2,a,cd0)

  xdt=xdt2
  f1=f1+delfbest                           !Improved estimate of DF

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> twk', xdt, f1, smax
    flush(6)
  endif

  call syncjs8d(cd0,icos,i0,0.0,sync)

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> ibest', ibest
    flush(6)
  endif

  j=0
  do k=1,NN
    i1=ibest+(k-1)*NDOWNSPS
    csymb=cmplx(0.0,0.0)
    if( i1.ge.0 .and. i1+(NDOWNSPS-1) .le. NP2-1 ) csymb=cd0(i1:i1+(NDOWNSPS-1))
    call four2a(csymb,NDOWNSPS,1,-1,1)
    s2(0:7,k)=abs(csymb(1:8))/1e3
  enddo  

  ! sync quality check
  is1=0
  is2=0
  is3=0
  do k=1,7
    ip=maxloc(s2(:,k))
    if(icos7a(k-1).eq.(ip(1)-1)) is1=is1+1
    ip=maxloc(s2(:,k+36))
    if(icos7b(k-1).eq.(ip(1)-1)) is2=is2+1
    ip=maxloc(s2(:,k+72))
    if(icos7c(k-1).eq.(ip(1)-1)) is3=is3+1
  enddo

  ! hard sync sum - max is 21
  nsync=is1+is2+is3

  if(NWRITELOG.eq.1) then
    write(*,*) '<DecodeDebug> sync', ibest, nsync
    write(*,*) '<DecodeDebug> icos', icos7a
    flush(6)
  endif

  if(nsync .le. 6) then ! bail out
    call timer('badnsync', 0)
    nbadcrc=1
    call timer('badnsync', 1)

    if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug> bad sync', f1, xdt, nsync
        flush(6)
    endif

    return
  endif

  if(syncStats) then
    write(*,*) '<DecodeSyncStat> candidate ', NSUBMODE, 'f1', f1, 'sync', nsync, 'xdt', xdt
    flush(6)
  endif

  j=0
  do k=1,NN
     if(k.le.7) cycle
     if(k.ge.37 .and. k.le.43) cycle
     if(k.gt.72) cycle
     j=j+1
     s1(0:7,j)=s2(0:7,k)
  enddo  

  call indexx(s1sort,8*ND,indxs1)

  xmeds1=s1sort(indxs1(nint(0.5*8*ND)))
  s1=s1/xmeds1

  do j=1,ND
     i4=3*j-2
     i2=3*j-1
     i1=3*j

     ! Max amplitude
     ps=s1(0:7,j)
     r1=max(ps(1),ps(3),ps(5),ps(7))-max(ps(0),ps(2),ps(4),ps(6))
     r2=max(ps(2),ps(3),ps(6),ps(7))-max(ps(0),ps(1),ps(4),ps(5))
     r4=max(ps(4),ps(5),ps(6),ps(7))-max(ps(0),ps(1),ps(2),ps(3))
     bmeta(i4)=r4
     bmeta(i2)=r2
     bmeta(i1)=r1
     bmetap(i4)=r4
     bmetap(i2)=r2
     bmetap(i1)=r1

     ! Max log metric
     psl=log(ps+1e-32)
     r1=max(psl(1),psl(3),psl(5),psl(7))-max(psl(0),psl(2),psl(4),psl(6))
     r2=max(psl(2),psl(3),psl(6),psl(7))-max(psl(0),psl(1),psl(4),psl(5))
     r4=max(psl(4),psl(5),psl(6),psl(7))-max(psl(0),psl(1),psl(2),psl(3))
     bmetb(i4)=r4
     bmetb(i2)=r2
     bmetb(i1)=r1

     ! ! Metric for Cauchy noise
     ! r1=log(ps(1)**3+ps(3)**3+ps(5)**3+ps(7)**3)- &
     !    log(ps(0)**3+ps(2)**3+ps(4)**3+ps(6)**3)
     ! r2=log(ps(2)**3+ps(3)**3+ps(6)**3+ps(7)**3)- &
     !    log(ps(0)**3+ps(1)**3+ps(4)**3+ps(5)**3)
     ! r4=log(ps(4)**3+ps(5)**3+ps(6)**3+ps(7)**3)- &
     !    log(ps(0)**3+ps(1)**3+ps(2)**3+ps(3)**3)
     !
     ! ! Metric for AWGN, no fading
     ! bscale=2.5
     ! b0=bessi0(bscale*ps(0))
     ! b1=bessi0(bscale*ps(1))
     ! b2=bessi0(bscale*ps(2))
     ! b3=bessi0(bscale*ps(3))
     ! b4=bessi0(bscale*ps(4))
     ! b5=bessi0(bscale*ps(5))
     ! b6=bessi0(bscale*ps(6))
     ! b7=bessi0(bscale*ps(7))
     ! r1=log(b1+b3+b5+b7)-log(b0+b2+b4+b6)
     ! r2=log(b2+b3+b6+b7)-log(b0+b1+b4+b5)
     ! r4=log(b4+b5+b6+b7)-log(b0+b1+b2+b3)

      ! When bits 88:115 are set as ap bits, bit 115 lives in symbol 39 along
      ! with no-ap bits 116 and 117. Take care of metrics for bits 116 and 117.
      if(j.eq.39) then  ! take care of bits that live in symbol 39
        if(apsym(28).lt.0) then
            bmetap(i2)=max(ps(2),ps(3))-max(ps(0),ps(1))
            bmetap(i1)=max(ps(1),ps(3))-max(ps(0),ps(2))
        else 
            bmetap(i2)=max(ps(6),ps(7))-max(ps(4),ps(5))
            bmetap(i1)=max(ps(5),ps(7))-max(ps(4),ps(6))
        endif
      endif
 
      ! When bits 116:143 are set as ap bits, bit 115 lives in symbol 39 along
      ! with ap bits 116 and 117. Take care of metric for bit 115.
      if(j.eq.39) then  ! take care of bit 115
         iii=2*(apsym(29)+1)/2 + (apsym(30)+1)/2  ! known values of bits 116 & 117
         if(iii.eq.0) bmetap(i4)=ps(4)-ps(0)
         if(iii.eq.1) bmetap(i4)=ps(5)-ps(1)
         if(iii.eq.2) bmetap(i4)=ps(6)-ps(2)
         if(iii.eq.3) bmetap(i4)=ps(7)-ps(3)
      endif
 
      ! bit 144 lives in symbol 48 and will be 1 if it is set as an ap bit.
      ! take care of metrics for bits 142 and 143
      if(j.eq.48) then  ! bit 144 is always 1
        bmetap(i4)=max(ps(5),ps(7))-max(ps(1),ps(3))
        bmetap(i2)=max(ps(3),ps(7))-max(ps(1),ps(5))
      endif 
  
      ! bit 154 lives in symbol 52 and will be 0 if it is set as an ap bit
      ! take care of metrics for bits 155 and 156
      if(j.eq.52) then  ! bit 154 will be 0 if it is set as an ap bit.
         bmetap(i2)=max(ps(2),ps(3))-max(ps(0),ps(1))
         bmetap(i1)=max(ps(1),ps(3))-max(ps(0),ps(2))
      endif  

  enddo

  call normalizebmet(bmeta,3*ND)
  call normalizebmet(bmetb,3*ND)
  call normalizebmet(bmetap,3*ND)

  scalefac=2.83
  llr0=scalefac*bmeta
  llr1=scalefac*bmetb
  llra=scalefac*bmetap  ! llr's for use with ap
  apmag=scalefac*(maxval(abs(bmetap))*1.01)

  ! pass #
  !------------------------------
  !   1        regular decoding
  !   2        erase 24
  !   3        erase 48
  !   4        ap pass 1
  !   5        ap pass 2
  !   6        ap pass 3
  !   7        ap pass 4, etc.

  do ipass=1,4 
               
     llr=llr0
     if(ipass.eq.2) llr=llr1
     if(ipass.eq.3) llr(1:24)=0. 
     if(ipass.eq.4) llr(1:48)=0. 
     if(ipass.le.4) then
        llrap=llr
        iaptype=0
     endif
        
     cw=0
     call timer('bpd174  ',0)
     call bpdecode174(llrap,max_iterations,decoded,cw,nharderrors,  &
          niterations)
     call timer('bpd174  ',1)

     if(NWRITELOG.eq.1) then
       write(*,*) '<DecodeDebug> bpd174', ipass, nharderrors, dmin
       flush(6)
     endif

     dmin=0.0
     if(ndepth.ge.3 .and. nharderrors.lt.0) then
        ndeep=3
        if(abs(nfqso-f1).le.napwid) then
          if((ipass.eq.3 .or. ipass.eq.4) .and. .not.nagain) then
            ndeep=3 
          else   
            ndeep=4  
          endif
        endif
        if(nagain) ndeep=5
        call timer('osd174  ',0)
        call osd174(llrap,ndeep,decoded,cw,nharderrors,dmin)
        call timer('osd174  ',1)
     endif
     nbadcrc=1
     message='                      '
     xsnr=-99.0

     if(NWRITELOG.eq.1) then
       write(*,*) '<DecodeDebug> osd174', ipass, nharderrors, dmin, nharderrors + dmin, sync
       flush(6)
     endif

     if(count(cw.eq.0).eq.174) cycle           !Reject the all-zero codeword
     if(nharderrors.ge.0 .and. nharderrors+dmin.lt.60.0 .and. &        
        .not.(sync.lt.2.0 .and. nharderrors.gt.35)      .and. &
        .not.(ipass.gt.2 .and. nharderrors.gt.39)       .and. &
        .not.(ipass.eq.4 .and. nharderrors.gt.30)             &
       ) then
        call chkcrc12a(decoded,nbadcrc)

        if(NWRITELOG.eq.1) then
          write(*,*) '<DecodeDebug> crc', decoded, 'bad', nbadcrc
          flush(6)
        endif
     else
        nharderrors=-1
        cycle 
     endif
     i3bit=4*decoded(73) + 2*decoded(74) + decoded(75)

     if(nbadcrc.eq.0) then
        if(syncStats) then
            write(*,*) '<DecodeSyncStat> decode ', NSUBMODE, 'f1', f1, 'sync', (sync*10), 'xdt', xdt2
            flush(6)
        endif

        decoded0=decoded
        call extractmessage174(decoded,origmsg,ncrcflag)
        decoded=decoded0

        message(1:12)=origmsg(1:12)
        call genjs8(message,icos,i3bit,msgsent,msgbits,itone)
        if(lsubtract) then
            if(NWRITELOG.eq.1) then
                write(*,*) '<DecodeDebug> subtract', f1, xdt2, itone
                flush(6)
            endif
            call subtractjs8(dd0,itone,f1,xdt2)
        endif
        xsig=0.0
        xnoi=0.0
        do i=1,NN
           xsig=xsig+s2(itone(i),i)**2
           ios=mod(itone(i)+4,7)
           xnoi=xnoi+s2(ios,i)**2
        enddo

        if(NWRITELOG.eq.1) then
            write(*,*) '<DecodeDebug> snr', xnoi, xsig, xbase, db(xsig/xbase), db(xsig/xbase - 1.0)
            flush(6)
        endif

        xsnr=0.001
        if(xnoi.gt.0 .and. xnoi.lt.xsig) xsnr=xsig/xnoi-1.0
        xsnr=10.0*log10(xsnr)-27.0
        xsnr2=db(xsig/xbase - 1.0) - 32.0
        if(.not.nagain) xsnr=xsnr2
        if(xsnr .lt. -28.0) xsnr=-28.0

        msg37=origmsg//'               '

        msg37(22:22) = char(48 + i3bit)

        if(NWRITELOG.eq.1) then
            write(*,*) '<DecodeDebug> msg', msg37, 'snr', xsnr
            flush(6)
        endif
        
        return
     endif
  enddo

  return
end subroutine js8dec

subroutine normalizebmet(bmet,n)
  real bmet(n)

  bmetav=sum(bmet)/real(n)
  bmet2av=sum(bmet*bmet)/real(n)
  var=bmet2av-bmetav*bmetav
  if( var .gt. 0.0 ) then
     bmetsig=sqrt(var)
  else
     bmetsig=sqrt(bmet2av)
  endif
  bmet=bmet/bmetsig
  return
end subroutine normalizebmet


function bessi0(x) 
! From Numerical Recipes
   real bessi0,x
   double precision p1,p2,p3,p4,p5,p6,p7,q1,q2,q3,q4,q5,q6,q7,q8,q9,y
   save p1,p2,p3,p4,p5,p6,p7,q1,q2,q3,q4,q5,q6,q7,q8,q9
   data p1,p2,p3,p4,p5,p6,p7/1.0d0,3.5156229d0,3.0899424d0,1.2067492d0, &
      0.2659732d0,0.360768d-1,0.45813d-2/
   data q1,q2,q3,q4,q5,q6,q7,q8,q9/0.39894228d0,0.1328592d-1,           &
      0.225319d-2,-0.157565d-2,0.916281d-2,-0.2057706d-1,               &
      0.2635537d-1,-0.1647633d-1,0.392377d-2/

   if (abs(x).lt.3.75) then 
      y=(x/3.75)**2
      bessi0=p1+y*(p2+y*(p3+y*(p4+y*(p5+y*(p6+y*p7))))) 
   else
      ax=abs(x)
      y=3.75/ax 
      bessi0=(exp(ax)/sqrt(ax))*(q1+y*(q2+y*(q3+y*(q4         &
           +y*(q5+y*(q6+y*(q7+y*(q8+y*q9))))))))
   endif
   return
end function bessi0
