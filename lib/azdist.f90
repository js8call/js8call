subroutine azdist(MyGrid,HisGrid,utch,nAz,nEl,nDmiles,nDkm,nHotAz,nHotABetter)

  character(len=*) :: MyGrid,HisGrid
  character*6 mygrid0,hisgrid0
  real*8 utch,utch0
  logical HotABetter,IamEast
  real eltab(22),daztab(22)
  data eltab/18.,15.,13.,11.,9.,8.,7.,6.,5.3,4.7,4.,3.3,2.7,    &
       2.,1.5,1.,0.8,0.6,0.4,0.2,0.0,0.0/
  data daztab/21.,18.,16.,15.,14.,13.,12.,11.,10.7,10.3,10.,    &
       10.,10.,10.,10.,10.,10.,9.,9.,9.,8.,8./
  data mygrid0/"      "/,hisgrid0/"      "/,utch0/-999.d0/
  save

  if(MyGrid.eq.HisGrid) then
     naz=0
     nel=0
     ndmiles=0
     ndkm=0
     nhotaz=0
     nhotabetter=1
     go to 999
  endif

  if(mygrid.eq.mygrid0 .and. hisgrid.eq.hisgrid0 .and.              &
       abs(utch-utch0).lt.0.1666667d0) go to 900
  utch0=utch
  mygrid0=mygrid
  hisgrid0=hisgrid
  utchours=utch

  if(MyGrid(5:5).eq.' ') MyGrid(5:5)='m'
  if(MyGrid(6:6).eq.' ') MyGrid(6:6)='m'
  if(HisGrid(5:5).eq.' ') HisGrid(5:5)='m'
  if(HisGrid(6:6).eq.' ') HisGrid(6:6)='m'

  if(MyGrid.eq.HisGrid) then
     Az=0.
     Dmiles=0.
     Dkm=0.0
     El=0.
     HotA=0.
     HotB=0.
     HotABetter=.true.
     go to 900
  endif
  call grid2deg(MyGrid,dlong1,dlat1)
  call grid2deg(HisGrid,dlong2,dlat2)
  eps=1.e-6
  Az=0.
  Dmiles=0.
  Dkm=0.0
  El=0.
  HotA=0.
  HotB=0.
  HotABetter=.true.
  if(abs(dlat1-dlat2).lt.eps .and. abs(dlong1-dlong2).lt.eps) go to 900

  difflong=mod(dlong1-dlong2+720.0,360.0)
  if(abs(dlat1+dlat2).lt.eps .and. abs(difflong-180.0).lt.eps) then
! Antipodes
     Dkm=20400
     go to 900
  endif

  call geodist(dlat1,dlong1,dlat2,dlong2,Az,Baz,Dkm)

  ndkm=Dkm/100
  j=ndkm-4
  if(j.lt.1) j=1
  if(j.gt.21)j=21
  if(Dkm.lt.500.0) then
     El=18.0
  else
     u=(Dkm-100.0*ndkm)/100.0
     El=(1.0-u)*eltab(j) + u*eltab(j+1)
  endif

  daz=daztab(j) + u * (daztab(j+1)-daztab(j))
  Dmiles=Dkm/1.609344

  tmid=mod(UTChours-0.5*(dlong1+dlong2)/15.0+48.0,24.0)
  IamEast=.false.
  if(dlong1.lt.dlong2) IamEast=.true.
  if(dlong1.eq.dlong2 .and. dlat1.gt.dlat2) IamEast=.false.
  azEast=baz
  if(IamEast) azEast=az
  if((azEast.ge.45.0 .and. azEast.lt.135.0) .or.                    &
       (azEast.ge.225.0 .and. azEast.lt.315.0)) then
! The path will be taken as "east-west".
     HotABetter=.true.
     if(abs(tmid-6.0).lt.6.0) HotABetter=.false.
     if((dlat1+dlat2)/2.0 .lt. 0.0) HotABetter=.not.HotABetter
  else
! The path will be taken as "north-south".
     HotABetter=.false.
     if(abs(tmid-12.0).lt.6.0) HotABetter=.true.
  endif
  if(IamEast) then
     HotA = Az - daz
     HotB = Az + daz
  else
     HotA = Az + daz
     HotB = Az - daz
  endif
  if(HotA.lt.0.0)   HotA=HotA+360.0
  if(HotA.gt.360.0) HotA=HotA-360.0
  if(HotB.lt.0.0)   HotB=HotB+360.0
  if(HotB.gt.360.0) HotB=HotB-360.0

900 continue
  naz=nint(Az)
  nel=nint(el)
  nDmiles=nint(Dmiles)
  nDkm=nint(Dkm)
  nHotAz=nint(HotB)
  nHotABetter=0
  if(HotABetter) then
     nHotAz=nint(HotA)
     nHotABetter=1
  endif

999 return
end subroutine azdist
