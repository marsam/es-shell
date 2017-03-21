#! /usr/local/bin/es --

fn t cmd-string want {
  let (res = '') {
    if {!es -c 'eval '''^$^cmd-string^'''' > /tmp/es-test >[2] /dev/null} {
      echo '[31mfailed[0m:' $cmd-string '(syntax error)'
      return
    } {
      res = `{cat /tmp/es-test}
    }
    if {~ $^want $^res} {
      echo 'passed:' $cmd-string
    } {
      echo '[31mfailed[0m:' $cmd-string
      echo ' - want' $want
      echo ' - got' $res
    }
  }
}

t 'a=b; echo $a' 'b'
t 'echo a=b' 'a=b'
t 'echo a=b; a=b; echo $a' 'a=b
b'
t 'let (a=b) echo $a' 'b'
t 'let (a=b;c=d) echo $a, $c' 'b, d'
t 'echo a=b & a=c; echo $a' 'a=b
c'
t 'a=b || a=c; echo $a' 'c'
t 'if {~ a a} {a=b; echo $a}' 'b'
t '(a b)=(c=d d=e); echo $b $a' 'd=e c=d'
