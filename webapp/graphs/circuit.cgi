#!/usr/bin/rrdcgi
 <RRD::SETVAR rrdb /var/lib/rwchcd/log_hcircuit_<RRD::CV::PATH hcircuit_name>>
 <RRD::SETVAR name <RRD::CV::PATH hcircuit_name>>
 <RRD::SETVAR width 1000>
 <RRD::SETVAR height 400>
 <RRD::SETVAR cdeftconv 0,+>
 <RRD::GOODFOR 300>
 <HTML>
 <HEAD>
 <TITLE>Circuit</TITLE>
 <meta name="viewport" content="width=device-width, initial-scale=1.0" />
 </HEAD>
 <BODY>
 <H1>Circuit</H1>
 <P>
 <RRD::GRAPH tmp/rrd-hcircuit-<RRD::GETVAR name>-2d.svg -a SVG -s -2d --lazy --title="2d" -E
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:takelth=<RRD::GETVAR rrdb>:target_ambient:LAST
 	DEF:aakelth=<RRD::GETVAR rrdb>:actual_ambient:LAST
 	DEF:twkelth=<RRD::GETVAR rrdb>:target_wtemp:LAST
 	DEF:awkelth=<RRD::GETVAR rrdb>:actual_wtemp:LAST
	CDEF:tacel=takelth,<RRD::GETVAR cdeftconv>,0,50,LIMIT
	CDEF:aacel=aakelth,<RRD::GETVAR cdeftconv>,-20,50,LIMIT
	CDEF:twcel=twkelth,<RRD::GETVAR cdeftconv>,0,100,LIMIT
	CDEF:awcel=awkelth,<RRD::GETVAR cdeftconv>
 	AREA:twcel#f7ad0011:skipscale
 	LINE1:tacel#0000a0:"T cible ambiant"
 	LINE1:aacel#00a0a0:"T actuel ambiant"
 	LINE1:twcel#f7ad00:"T cible circuit"
 	LINE1:awcel#a00000:"T actuel circuit"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-hcircuit-<RRD::GETVAR name>-1w.svg -a SVG -s -1w --lazy --title="1w" -E
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:takelth=<RRD::GETVAR rrdb>:target_ambient:LAST
 	DEF:aakelth=<RRD::GETVAR rrdb>:actual_ambient:LAST
 	DEF:twkelth=<RRD::GETVAR rrdb>:target_wtemp:LAST
 	DEF:awkelth=<RRD::GETVAR rrdb>:actual_wtemp:LAST
	CDEF:tacel=takelth,<RRD::GETVAR cdeftconv>,0,50,LIMIT
	CDEF:aacel=aakelth,<RRD::GETVAR cdeftconv>,-20,50,LIMIT
	CDEF:twcel=twkelth,<RRD::GETVAR cdeftconv>,0,100,LIMIT
	CDEF:awcel=awkelth,<RRD::GETVAR cdeftconv>
 	AREA:twcel#f7ad0011:skipscale
 	LINE1:tacel#0000a0:"T cible ambiant"
 	LINE1:aacel#00a0a0:"T actuel ambiant"
 	LINE1:twcel#f7ad00:"T cible circuit"
 	LINE1:awcel#a00000:"T actuel circuit"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-hcircuit-<RRD::GETVAR name>-1m.svg -a SVG -s -1m --lazy --title="1m" -E
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:takelth=<RRD::GETVAR rrdb>:target_ambient:AVERAGE
 	DEF:aakelth=<RRD::GETVAR rrdb>:actual_ambient:AVERAGE
 	DEF:twkelth=<RRD::GETVAR rrdb>:target_wtemp:AVERAGE
 	DEF:awkelth=<RRD::GETVAR rrdb>:actual_wtemp:AVERAGE
	CDEF:tacel=takelth,<RRD::GETVAR cdeftconv>,0,50,LIMIT
	CDEF:aacel=aakelth,<RRD::GETVAR cdeftconv>,-20,50,LIMIT
	CDEF:twcel=twkelth,<RRD::GETVAR cdeftconv>,0,100,LIMIT
	CDEF:awcel=awkelth,<RRD::GETVAR cdeftconv>
 	AREA:twcel#f7ad0011:skipscale
 	LINE1:tacel#0000a0:"T cible ambiant"
 	LINE1:aacel#00a0a0:"T actuel ambiant"
 	LINE1:twcel#f7ad00:"T cible circuit"
 	LINE1:awcel#a00000:"T actuel circuit"
 >
 </P>
 </BODY>
 </BODY>
 </HTML>
