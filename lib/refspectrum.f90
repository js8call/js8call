subroutine refspectrum(id2,bclear,brefspec,buseref,fname)

! Input:
!  id2       i*2        Raw 16-bit integer data, 12000 Hz sample rate
!  brefspec  logical    True when accumulating a reference spectrum

  parameter (NFFT=6912,NH=NFFT/2,NPOLYLOW=400,NPOLYHIGH=2600)
  integer*2 id2(NFFT)
  logical*1 bclear,brefspec,buseref,blastuse
  
  real xs(0:NH-1)                         !Saved upper half of input chunk convolved with h(t) 
  real x(0:NFFT-1)                        !Work array
  real*4 w(0:NFFT-1)                      !Window function
  real*4 s(0:NH)                          !Average spectrum
  real*4 fil(0:NH)
  real*8 xfit(1500),yfit(1500),sigmay(1500),a(5),chisqr !Polyfit arrays
  logical first
  complex cx(0:NH)                        !Complex frequency-domain work array
  complex cfil(0:NH)
  character*(*) fname
  common/spectra/syellow(6827),ref(0:NH),filter(0:NH)
  equivalence(x,cx)
  data first/.true./,blastuse/.false./
  save

  if(first) then
     pi=4.0*atan(1.0)
     do i=0,NFFT-1
        ww=sin(i*pi/NFFT)
        w(i)=ww*ww/NFFT
     enddo
     nsave=0
     s=0.0
     filter=1.0
     xs=0.
     first=.false.
  endif
  if(bclear) s=0.

  if(brefspec) then
     x(0:NH-1)=0.001*id2(1:NH)
     x(NH:NFFT-1)=0.0
     call four2a(cx,NFFT,1,-1,0)                 !r2c FFT

     do i=1,NH
        s(i)=s(i) + real(cx(i))**2 + aimag(cx(i))**2
     enddo
     nsave=nsave+1

     fac0=0.9
     if(mod(nsave,4).eq.0) then
        df=12000.0/NFFT
        ia=nint(1000.0/df)
        ib=nint(2000.0/df)
        avemid=sum(s(ia:ib))/(ib-ia+1)
        do i=0,NH
           fil(i)=0.
           if(s(i).gt.0.0) then
              fil(i)=sqrt(avemid/s(i))
           endif
        enddo

! Default range is 240 - 4000 Hz.  For narrower filters, use frequencies
! at which gain is -20 dB relative to 1500 Hz.
        ia=nint(240.0/df)
        ib=nint(4000.0/df)
        i0=nint(1500.0/df)
        do i=i0,ia,-1
           if(s(i)/s(i0).lt.0.01) exit
        enddo
        ia=i
        do i=i0,ib,1
           if(s(i)/s(i0).lt.0.01) exit
        enddo
        ib=i

        fac=fac0
        do i=ia,1,-1
           fac=fac*fac0
           fil(i)=fac*fil(i)
        enddo

        fac=fac0
        do i=ib,NH
           fac=fac*fac0
           fil(i)=fac*fil(i)
        enddo

        do iter=1,100                        !### ??? ###
           call smo121(fil,NH)
        enddo

        do i=0,NH 
           filter(i)=-60.0
           if(s(i).gt.0.0) filter(i)=20.0*log10(fil(i))
        enddo

        il=nint(NPOLYLOW/df)
        ih=nint(NPOLYHIGH/df)
        nfit=ih-il+1
        mode=0
        nterms=5
        do i=1,nfit
          xfit(i)=((i+il-1)*df-1500.0)/1000.0
          yfit(i)=fil(i+il-1)
          sigmay(i)=1.0
        enddo
        call polyfit(xfit,yfit,sigmay,nfit,nterms,mode,a,chisqr)

        open(16,file=fname,status='unknown')
        write(16,1003) NPOLYLOW,NPOLYHIGH,nterms,a
1003    format(3i5,5e25.16)
        do i=1,NH
           freq=i*df
           ref(i)=db(s(i)/avemid)
           write(16,1005) freq,s(i),ref(i),fil(i),filter(i)
1005       format(f10.3,e12.3,f12.6,e12.3,f12.6)
        enddo
        close(16)
     endif
     return
  endif

  if(buseref) then
     if(blastuse.neqv.buseref) then !just enabled so read filter
        fil=1.0
        open(16,file=fname,status='old',err=999)
        read(16,1003,err=20,end=999) ndummy,ndummy,nterms,a
        goto 30
20      rewind(16)              !allow for old style refspec.dat with no header
30      do i=1,NH
           read(16,1005,err=999,end=999) freq,s(i),ref(i),fil(i),filter(i)
        enddo
! Make the filter causal for overlap and add.
        cx(0)=0.0
        cx(1:NH)=fil(1:NH)/NFFT
        call four2a(cx,NFFT,1,1,-1)
        x=cshift(x,-400)
        x(800:NH)=0.0
        call four2a(cx,NFFT,1,-1,0)
        cfil=cx
        close(16)
     endif
! Use overlap and add method to apply causal reference filter.
     x(0:NH-1)=id2(1:NH)
     x(NH:NFFT-1)=0.0
     x=x/NFFT
     call four2a(cx,NFFT,1,-1,0)
     cx=cfil*cx
     call four2a(cx,NFFT,1,1,-1)
     x(0:NH-1)=x(0:NH-1)+xs    
     xs=x(NH:NFFT-1)
     id2(1:NH)=nint(x(0:NH-1))
  endif
  blastuse=buseref
  
999 return
end subroutine refspectrum
