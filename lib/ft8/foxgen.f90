subroutine foxgen()

  ! Called from MainWindow::foxTxSequencer() to generate the Tx waveform in
  ! FT8 Fox mode.  The Tx message can contain up to 5 "slots", each carrying
  ! its own FT8 signal.
  
  ! Encoded messages can be of the form "HoundCall FoxCall rpt" (a standard FT8
  ! message with i3bit=0) or "HoundCall_1 RR73; HoundCall_2 <FoxCall> rpt", 
  ! a new message type with i3bit=1.  The waveform is generated with
  ! fsample=48000 Hz; it is compressed to reduce the PEP-to-average power ratio,
  ! with (currently disabled) filtering afterware to reduce spectral growth.

  ! Input message information is provided in character array cmsg(5), in
  ! common/foxcom/.  The generated wave(NWAVE) is passed back in the same
  ! common block.
  
  use crc
  parameter (NN=79,ND=58,KK=87,NSPS=4*1920)
  parameter (NWAVE=NN*NSPS,NFFT=614400,NH=NFFT/2)
  parameter (OFFSET=25)
  character*40 cmsg
  character*22 msg,msgsent
  integer itone(NN)
  integer icos7(0:6)
  integer*1 msgbits(KK),msgbits2
  integer*1, target:: mycall
  real x(NFFT)
  real*8 dt,twopi,f0,fstep,dfreq,phi,dphi
  complex cx(0:NH)
  common/foxcom/wave(NWAVE),nslots,nfreq,i3bit(5),cmsg(5),mycall(12)
  common/foxcom2/itone2(NN),msgbits2(KK)
  equivalence (x,cx),(y,cy)
  data icos7/4,2,5,6,1,3,0/                   !Costas 7x7 tone pattern

  width=50.0
  fstep=width+OFFSET
  dfreq=6.25d0
  dt=1.d0/48000.d0
  twopi=8.d0*atan(1.d0)
  irpt=0
  nplot=0
  wave=0.
  k=0

  do n=1,nslots
     i3b=i3bit(n)
     !if(i3b.eq.0) then
        msg=cmsg(n)(1:12)                     !Standard FT8 message
     !else
     !   i1=index(cmsg(n),' ')                 !Special Fox message
     !   i2=index(cmsg(n),';')
     !   i3=index(cmsg(n),'<')
     !   i4=index(cmsg(n),'>')
     !   msg=cmsg(n)(1:i1)//cmsg(n)(i2+1:i3-2)//'                   '
     !   read(cmsg(n)(i4+2:i4+4),*) irpt
     !endif
     call genft8(msg,i3b,msgsent,msgbits,itone)
!     print*,'Foxgen:',n,cmsg(n),msgsent

!!      if(i3b.eq.1) then
!!         icrc10=crc10(c_loc(mycall),12)
!!         nrpt=irpt+30
!!         write(cbits,1001) msgbits(1:56),icrc10,nrpt,i3b,0
!! 1001    format(56b1.1,b10.10,b6.6,b3.3,b12.12)
!!         read(cbits,1002) msgbits
!! 1002    format(87i1)
!! 
!!         cb88=cbits//'0'
!!         read(cb88,1003) i1Msg8BitBytes(1:11)
!! 1003    format(11b8)
!!         icrc12=crc12(c_loc(i1Msg8BitBytes),11)
!! !        icrc12=xor(icrc12, 41) ! TODO: jsherer - could change the crc here
!! 
!!         write(cbits,1001) msgbits(1:56),icrc10,nrpt,i3b,icrc12
!!         read(cbits,1002) msgbits
!! 
!!         call encode174(msgbits,codeword)      !Encode the test message
!!         
!! ! Message structure: S7 D29 S7 D29 S7
!!         itone(1:7)=icos7
!!         itone(36+1:36+7)=icos7
!!         itone(NN-6:NN)=icos7
!!         k=7
!!         do j=1,ND
!!            i=3*j -2
!!            k=k+1
!!            if(j.eq.30) k=k+7
!!            itone(k)=codeword(i)*4 + codeword(i+1)*2 + codeword(i+2)
!!         enddo
!!      endif
     
! Make copies of itone() and msgbits() for ft8sim
     itone2=itone
     msgbits2=msgbits
     f0=nfreq + fstep*(n-1)
     phi=0.d0
     k=0
     do j=1,NN
        f=f0 + dfreq*itone(j)
        dphi=twopi*f*dt
        do ii=1,NSPS
           k=k+1
           phi=phi+dphi
           xphi=phi
           wave(k)=wave(k)+sin(xphi)
        enddo
     enddo
  enddo
  kz=k
  
  peak1=maxval(abs(wave))
  wave=wave/peak1
!  call plotspec(1,wave)          !Plot the spectrum

! Apply compression
  rms=sqrt(dot_product(wave,wave)/kz)
  wave=wave/rms
  do i=1,NWAVE
     wave(i)=h1(wave(i))
  enddo
  peak2=maxval(abs(wave))
  wave=wave/peak2
  
!  call plotspec(2,wave)          !Plot the spectrum

  call foxfilt(nslots,nfreq,width,wave)
  peak3=maxval(abs(wave))
  wave=wave/peak3

!  nadd=1000
!  j=0
!  do i=1,NWAVE,nadd
!     sx=dot_product(wave(i:i+nadd-1),wave(i:i+nadd-1))
!     j=j+1
!     write(30,3001) j,sx/nadd
!3001 format(i8,f12.6)
!  enddo

!  call plotspec(3,wave)          !Plot the spectrum
  
  return
end subroutine foxgen

! include 'plotspec.f90'
