subroutine polyfit(x,y,sigmay,npts,nterms,mode,a,chisqr)
  implicit real*8 (a-h,o-z)
  real*8 x(npts), y(npts), sigmay(npts), a(nterms)
  real*8 sumx(10), sumy(10), array(10,10)

! Accumulate weighted sums
  nmax = 2*nterms-1
  sumx=0.
  sumy=0.
  chisq=0.
  do i=1,npts
     xi=x(i)
     yi=y(i)
     if(mode.lt.0) then
        weight=1./abs(yi)
     else if(mode.eq.0) then
        weight=1
     else
        weight=1./sigmay(i)**2
     end if
     xterm=weight
     do n=1,nmax
        sumx(n)=sumx(n)+xterm
        xterm=xterm*xi
     enddo
     yterm=weight*yi
     do n=1,nterms
        sumy(n)=sumy(n)+yterm
        yterm=yterm*xi
     enddo
     chisq=chisq+weight*yi**2
  enddo

! Construct matrices and calculate coefficients
  do j=1,nterms
     do k=1,nterms
        n=j+k-1
        array(j,k)=sumx(n)
     enddo
  enddo

  delta=determ(array,nterms)
  if(delta.eq.0) then
     chisqr=0.
     a=0.
  else
     do l=1,nterms
        do j=1,nterms
           do k=1,nterms
              n=j+k-1
              array(j,k)=sumx(n)
           enddo
           array(j,l)=sumy(j)
        enddo
        a(l)=determ(array,nterms)/delta
     enddo

! Calculate chi square

     do j=1,nterms
        chisq=chisq-2*a(j)*sumy(j)
        do k=1,nterms
           n=j+k-1
           chisq=chisq+a(j)*a(k)*sumx(n)
        enddo
     enddo
     free=npts-nterms
     chisqr=chisq/free
  end if

  return
end subroutine polyfit
