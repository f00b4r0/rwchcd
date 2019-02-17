 <RRD::SETVAR rrdb /var/lib/rwchcd/log_hw_p1_temps>
 <RRD::SETVAR width 800>
 <RRD::SETVAR height 200>
 <RRD::SETVAR cdeftconv TIME,1550342839,GT,1024,1000,IF,/,273,->
 <RRD::GOODFOR 60>
 <HTML>
 <HEAD>
 <TITLE><RRD::GETVAR title></TITLE>
 <meta name="viewport" content="width=device-width, initial-scale=1.0" />
 </HEAD>
 <BODY>
 <H1><RRD::GETVAR title></H1>
 <P>
 <RRD::GRAPH tmp/rrd-<RRD::GETVAR DS>-4h.svg -a SVG -s -4h --lazy --slope-mode --title="4h"
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	HRULE:0#000000
 	DEF:celth=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:LAST
	CDEF:cel=celth,<RRD::GETVAR cdeftconv>
 	LINE1:cel#00a000:"Last"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-<RRD::GETVAR DS>-2d.svg -a SVG -s -2d --lazy --slope-mode --title="48h"
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	HRULE:0#000000
 	DEF:celth=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:AVERAGE
	CDEF:cel=celth,<RRD::GETVAR cdeftconv>
 	LINE1:cel#00a000:"Moy"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-<RRD::GETVAR DS>-2w.svg -a SVG -s -2w --lazy --slope-mode --title="15j"
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	HRULE:0#000000
 	DEF:celth=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:AVERAGE
	CDEF:cel=celth,<RRD::GETVAR cdeftconv>
 	LINE1:cel#00a000:"Moy"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-<RRD::GETVAR DS>-2M.svg -a SVG -s -2M --lazy --slope-mode --title="2M"
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	HRULE:0#000000
 	DEF:celth=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:AVERAGE
	CDEF:cel=celth,<RRD::GETVAR cdeftconv>
 	LINE1:cel#00a000:"Moy"
 >
 </P>
 <P>
 <RRD::GRAPH tmp/rrd-<RRD::GETVAR DS>-1y.svg -a SVG -s -1y --lazy --slope-mode --title="1A"
	--imginfo '<IMG SRC=tmp/%s WIDTH=%lu HEIGHT=%lu>'
	-w <RRD::GETVAR width> -h <RRD::GETVAR height>
	HRULE:0#000000
 	DEF:celth=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:AVERAGE
	CDEF:cel=celth,<RRD::GETVAR cdeftconv>
 	LINE1:cel#00a000:"Moy"
 	DEF:celthM=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:MAX
 	DEF:celthm=<RRD::GETVAR rrdb>:<RRD::GETVAR DS>:MIN
	CDEF:celM=celthM,<RRD::GETVAR cdeftconv>
	CDEF:celm=celthm,<RRD::GETVAR cdeftconv>
 	LINE1:celM#a00000:"Max"
 	LINE1:celm#0000a0:"Min"
 >
 </P>
 </BODY>
 </HTML>
