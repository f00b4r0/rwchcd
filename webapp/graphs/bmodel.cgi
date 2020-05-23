#!/usr/bin/rrdcgi
 <RRD::SETVAR rrdb /var/lib/rwchcd/log_models_bmodel_house>
 <RRD::SETVAR width 1000>
 <RRD::SETVAR height 600>
 <RRD::GOODFOR 300>
 <RRD::SETVAR cdeftconv TIME,1550342839,GT,1024,1000,IF,/,273,->
 <HTML>
 <HEAD>
 <TITLE>Bmodel</TITLE>
 <meta name="viewport" content="width=device-width, initial-scale=1.0" />
 </HEAD>
 <BODY>
 <H1>Bmodel</H1>
 <P>
 <RRD::GRAPH tmp/rrd-bmodel-1w.svg -a SVG -s -1w --lazy --title="1w" -E -A
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:tocelth=<RRD::GETVAR rrdb>:t_out:LAST
 	DEF:tofcelth=<RRD::GETVAR rrdb>:t_out_filt:LAST
 	DEF:tomcelth=<RRD::GETVAR rrdb>:t_out_mix:LAST
 	DEF:toacelth=<RRD::GETVAR rrdb>:t_out_att:LAST
	DEF:frost=<RRD::GETVAR rrdb>:frost:LAST
	DEF:summer=<RRD::GETVAR rrdb>:summer:LAST
	CDEF:tout=tocelth,<RRD::GETVAR cdeftconv>
	CDEF:toutfilt=tofcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutmix=tomcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutatt=toacelth,<RRD::GETVAR cdeftconv>
 	LINE1:tout#00a000:"T out"
 	LINE1:toutmix#a00800:"T mix"
 	LINE1:toutfilt#db8000:"T filt"
 	LINE1:toutatt#00a0a0:"T att"
	TICK:frost#88a8a888:0.1:"frost"
	TICK:summer#a8888888:-0.1:"summer"
	HRULE:0#000000
	HRULE:18#0ece0e80
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-bmodel-1m.svg -a SVG -s -1m --lazy --title="1m" -E -A
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:tocelth=<RRD::GETVAR rrdb>:t_out:LAST
 	DEF:tofcelth=<RRD::GETVAR rrdb>:t_out_filt:LAST
 	DEF:tomcelth=<RRD::GETVAR rrdb>:t_out_mix:LAST
 	DEF:toacelth=<RRD::GETVAR rrdb>:t_out_att:LAST
	DEF:frost=<RRD::GETVAR rrdb>:frost:LAST
	DEF:summer=<RRD::GETVAR rrdb>:summer:LAST
	CDEF:tout=tocelth,<RRD::GETVAR cdeftconv>
	CDEF:toutfilt=tofcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutmix=tomcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutatt=toacelth,<RRD::GETVAR cdeftconv>
 	LINE1:tout#00a000:"T out"
 	LINE1:toutmix#a00800:"T mix"
 	LINE1:toutfilt#db8000:"T filt"
 	LINE1:toutatt#00a0a0:"T att"
	TICK:frost#88a8a888:0.1:"frost"
	TICK:summer#a8888888:-0.1:"summer"
	HRULE:0#000000
	HRULE:18#0ece0e80
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-bmodel-1y.svg -a SVG -s -1y --lazy --title="1y" -E -A
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
 	DEF:tofcelth=<RRD::GETVAR rrdb>:t_out_filt:AVERAGE
 	DEF:tomcelth=<RRD::GETVAR rrdb>:t_out_mix:AVERAGE
 	DEF:toacelth=<RRD::GETVAR rrdb>:t_out_att:AVERAGE
	DEF:frost=<RRD::GETVAR rrdb>:frost:LAST
	DEF:summer=<RRD::GETVAR rrdb>:summer:LAST
	CDEF:toutfilt=tofcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutmix=tomcelth,<RRD::GETVAR cdeftconv>
	CDEF:toutatt=toacelth,<RRD::GETVAR cdeftconv>
 	LINE1:toutmix#a00800:"T mix"
 	LINE1:toutfilt#db8000:"T filt"
 	LINE1:toutatt#00a0a0:"T att"
	TICK:frost#88a8a888:0.1:"frost"
	TICK:summer#a8888888:-0.1:"summer"
	HRULE:0#000000
	HRULE:18#0ece0e80
 >
 </P>
 </BODY>
 </BODY>
 </BODY>
 </HTML>
