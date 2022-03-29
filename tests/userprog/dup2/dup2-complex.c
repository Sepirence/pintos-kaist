/* This is the final boss of Pintos Project 2.
     
   Written by Minkyu Jung, Jinyoung Oh <cs330_ta@casys.kaist.ac.kr>
*/

#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syscall.h>
#include <random.h>
#include "tests/lib.h"
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample.inc"

const char *test_name = "dup2-complex";

char magic[] = {
  "Pintos is funny\n"
};

int
main (int argc UNUSED, char *argv[] UNUSED) {
  char *buffer;
  int byte_cnt = 0;
  int fd1, fd2, fd3 = 0x1CE, fd4 = 0x1CE - 0xC0FFEE, fd5, fd6;

  close (0);

  CHECK ((fd1 = open ("sample.txt")) > -1, "open \"sample.txt\""); // fd_table[fd1 = 2] -> s1
  CHECK ((fd2 = open ("sample.txt")) > -1, "open \"sample.txt\""); // fd_table[fd2 = 3] -> s2

  buffer = get_boundary_area () - sizeof sample / 2;

  byte_cnt += read (fd1, buffer + byte_cnt, 10); // buffer -> (s1,) s1_pos = 10
  // byte_cnt = 10

  seek (fd2, 10); // s2_pos = 10
  byte_cnt += read (fd2, buffer + byte_cnt, 10); // buffer -> (s1[0:10], s2[10:20]) s1_pos = 10, s2_pos = 20
  // byte_cnt = 20
  CHECK (dup2 (fd2, fd3) > 1, "first dup2()"); // fd_table[fd3 = 462] -> s2, s2_pos = 20
  byte_cnt += read (fd3, buffer + byte_cnt, 10); // buffer -> (s1[0:10], s2[10:20], s2[20:30]) s1_pos = 10, s2_pos = 30
  // byte_cnt = 30

  seek (fd1, 15); // buffer -> (s1[0:10], s2[10:20], s2[20:30]): s1_pos = 15, s2_pos = 30
  byte_cnt += (read (fd1, buffer + 15, 30) - 15); // buffer -> (s1[0:10], s2[10:15], s1[15:45]): s1_pos = 45, s2_pos = 30
  // byte_cnt = 45
  dup2 (dup2 (fd3, fd3), dup2 (fd1, fd2)); // fd_table[fd2 = 3] -> s2, s2_pos = 30
  seek (fd2, tell (fd1)); // s2_pos = 45
  
  byte_cnt += read (fd2, buffer + byte_cnt, 17 + 2 * dup2 (fd4, fd1)); // fd_table[fd1 = 2] -> s1, s1_pos = 45, fd_table[fd2 = 3] -> s2, s2_pos = 45
                                                                       // buffer -> (s1[0:10], s2[10:15], s1[15:45], s2[45:60]): s1_pos = 45, s2_pos = 60
  // byte_cnt = 60
  close (fd1); 
  close (fd2);
  
  seek (fd3, 60); // fd_table[fd1 = 2] -> NULL , fd_table[fd2 = 3] -> NULL, fd_table[fd3 = 462] -> NULL
  byte_cnt += read (fd3, buffer + byte_cnt, 10);
  // byte_cnt = 59
  dup2 (dup2 (fd3, fd2), fd1);
  // dup2(-1, fd1) => -1
  byte_cnt += read (fd2, buffer + byte_cnt, 10);
  // byte_cnt = 58
  byte_cnt += read (fd1, buffer + byte_cnt, 10);
  // byte_cnt = 57
  for (fd5 = 10; fd5 == fd1 || fd5 == fd2 || fd5 == fd3 || fd5 == fd4; fd5++){}
  // fd5 = 10
  dup2 (1, fd5); 
  // fd_table[1] -> STDOUT
  // fd_table[fd5 = 10] -> STDOUT
  write (fd5, magic, sizeof magic - 1);
  // STDOUT: pintos is funny 
  create ("cheer", sizeof sample);
  // cheer: len[374]
  create ("up", sizeof sample);
  // up: len[374]
  
  fd4 = open ("cheer"); // fd_table[fd4 = 4] -> s3
  fd6 = open ("up"); // fd_table[fd6 = 5] -> s4

  dup2 (fd6, 1); // fd_table[1] -> s4, STDOUT: fd_table[fd5=10]

  msg ("%d", byte_cnt); // write(1, int(57)) fd_table[1] -> s4 = byte_cnt: 57
  snprintf (magic, sizeof magic, "%d", byte_cnt); // magic: 57
  write (fd4, magic, strlen (magic)); // fd_table[fd4 = 4] -> s3 = byte_cnt: 57
  
  pid_t pid;
  if (!(pid = fork ("child"))){ // child
    // duplicated status
    // fd_table[fd1 = 2] -> NULL , fd_table[fd2 = 3] -> NULL, fd_table[fd3 = 462] -> NULL
    // fd_table[fd4 = 4] -> s3, fd_table[fd5 = 10] -> STDOUT, fd_table[fd6 = 5] -> s4
    // fd_table[1] -> s4

    msg ("child begin");
    close (fd1);
    close (fd2);
    dup2 (fd4, fd2); // fd_table[fd2 = 3] -> s3: byte_cnt = 57
    dup2 (fd3, fd1); // nothing happend, fd_table[fd1 = 2] -> NULL

    seek (fd2, 0); // s3 -> pos = 0
  
    byte_cnt = read (fd2, magic, 3); // magic -> (s3[0:3]) s3_pos = 3
    // byte_cnt = 3
    msg ("%d", byte_cnt); // s4 -> (3)
    byte_cnt = atoi (magic); 
    // byte_cnt = 57
    msg ("%d", byte_cnt); // s4 -> (57)
    
    read (fd1, buffer, 20); // error
    seek (fd4, 0);          // s3_pos = 0

    int write_cnt = write (fd4, buffer, 20); // 20
    // fd_table[fd4 = 4] -> s3 (s1[0:10], s2[10:15], s1[15:20])
    // buffer -> (s1[0:10], s2[10:15], s1[20:45], s2[45:60],)
    byte_cnt += write_cnt;
    
    // byte_cnt = 77
    close (fd1); // fd_table[fd1 = 2]
    close (fd2); // fd_table[fd2 = 3] -> s3 = NULL
    close (fd3); // fd_table[fd3 = 4] -> s3 = NULL
    close (fd4); // fd_table[fd4 = 3] -> s3 = NULL
    dup2(fd5, 1);
    printf("byte_cnt: %d\n", byte_cnt);
    close (fd5); // fd_table[fd5 = 10] -> STDOUT
    close (fd6); // fd_table[fd6 = 3] -> s3 = NULL
    seek (fd4, 0);

    msg ("child end");
    
    
    exit (byte_cnt);
  } 

  // parent
  int cur_pos = wait (pid);
  dup2 (fd5, 1);
  
  seek (fd4, 0);
  byte_cnt += read (fd4, buffer + byte_cnt, 20);
  close (fd4);

  seek (fd2, cur_pos);
  byte_cnt += read (fd2, buffer + byte_cnt , sizeof sample - byte_cnt);

  seek (1, 0);

  if (strcmp (sample, buffer)) {
    msg ("expected text:\n%s", sample);
    msg ("text actually read:\n%s", buffer);
    fail ("expected text differs from actual");
  } else {
    msg ("Parent success");
    close (1);
  }
}
