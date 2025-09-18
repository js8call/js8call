subroutine nuttal_window(win,n)
  real win(n)

  pi=4.0*atan(1.0)
  a0=0.3635819
  a1=-0.4891775;
  a2=0.1365995;
  a3=-0.0106411;
  do i=1,n
     win(i)=a0+a1*cos(2*pi*(i-1)/(n))+ &
          a2*cos(4*pi*(i-1)/(n))+ &
          a3*cos(6*pi*(i-1)/(n))
  enddo
  return
end subroutine nuttal_window
