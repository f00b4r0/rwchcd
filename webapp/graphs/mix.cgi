#!/usr/bin/rrdcgi
 <RRD::SETVAR rrdbc /var/lib/rwchcd/log_hcircuit_circuit>
 <RRD::SETVAR rrdbbm /var/lib/rwchcd/log_models_bmodel_house>
 <RRD::SETVAR rrdbhw /var/lib/rwchcd/log_hw_p1_temps>
 <RRD::SETVAR width 1200>
 <RRD::SETVAR height 600>
 <RRD::GOODFOR 300>
 <HTML>
 <HEAD>
 <TITLE>Mix</TITLE>
 <meta name="viewport" content="width=device-width, initial-scale=1.0" />
 </HEAD>
 <BODY>
 <H1>Mix</H1>
 <P>
 <RRD::GRAPH tmp/rrd-mix-1w.svg -a SVG -s -1w --lazy --title="1w" -E
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	DEF:tbokelth=<RRD::GETVAR rrdbhw>:s2:MIN
 	DEF:takelth=<RRD::GETVAR rrdbc>:target_ambient:LAST
 	DEF:aakelth=<RRD::GETVAR rrdbc>:actual_ambient:LAST
 	DEF:twkelth=<RRD::GETVAR rrdbc>:target_wtemp:LAST
 	DEF:awkelth=<RRD::GETVAR rrdbc>:actual_wtemp:LAST
	DEF:tomkelth=<RRD::GETVAR rrdbbm>:t_out_mix:LAST
	CDEF:tbocel=tbokelth,1000,/,273.15,-
	CDEF:tacel=takelth,1000,/,273.15,-
	CDEF:aacel=aakelth,1000,/,273.15,-
	CDEF:twcel=twkelth,1000,/,273.15,-,0,100,LIMIT
	CDEF:awcel=awkelth,1000,/,273.15,-
	CDEF:toutmix=tomkelth,1000,/,273.15,-
 	AREA:twcel#00a00011:skipscale
 	LINE1:tbocel#c48880:"T chaud min"
 	LINE1:tacel#0000a0:"T cible ambiant"
 	LINE1:tacel#0000a0:"T cible ambiant"
 	LINE1:aacel#00a0a0:"T actuel ambiant"
 	LINE1:twcel#00a000:"T cible circuit"
 	LINE1:awcel#a00000:"T actuel circuit"
	LINE1:toutmix#db8000:"T out mix"
	HRULE:0#000000
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-mix2-1w.svg -a SVG -s -1w --lazy --title="1w" -E
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:takelth=<RRD::GETVAR rrdbc>:target_ambient:LAST
 	DEF:twkelth=<RRD::GETVAR rrdbc>:target_wtemp:LAST
	DEF:tokelth=<RRD::GETVAR rrdbbm>:t_out:LAST
	DEF:tomkelth=<RRD::GETVAR rrdbbm>:t_out_mix:LAST
	DEF:frost=<RRD::GETVAR rrdbbm>:frost:LAST
	DEF:summer=<RRD::GETVAR rrdbbm>:summer:LAST
	CDEF:tacel=takelth,1000,/,273.15,-
	CDEF:twcel=twkelth,1000,/,273.15,-,0,100,LIMIT
	CDEF:tout=tokelth,1000,/,273.15,-
	CDEF:toutmix=tomkelth,1000,/,273.15,-
	CDEF:thresh=tacel,18,GE,18.5,tacel,13,LE,7.5,16,IF,IF,1,-
 	AREA:twcel#00a00011:skipscale
	LINE1:tout#db8080:"T out"
	LINE1:toutmix#db8000:"T out mix"
	LINE1:thresh::skipscale
	AREA:1#88888888:"coupure":STACK:skipscale
	TICK:frost#88a8a888:0.1:"frost"
	TICK:summer#a8888888:-0.1:"summer"
	HRULE:3#db808080
	HRULE:0#000000
 >
 </P>
 </BODY>
 </BODY>
 </HTML>
