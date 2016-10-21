#!/usr/bin/python

# License: GPLv2

import web
from web import form
from pydbus import SystemBus

bus = SystemBus()
rwchcd = bus.get('org.slashdirt.rwchcd', '/org/slashdirt/rwchcd')
rwchcd_Control = rwchcd['org.slashdirt.rwchcd.Control']

render = web.template.render('templates/')

urls = (
	'/', 'rwchcd'
)

formMode = form.Form(
	form.Dropdown('sysmode', [(0, 'Off'), (2, 'Confort'), (3, 'Eco'), (4, 'Hors-Gel'), (5, 'ECS')])
	)

class rwchcd:
	def GET(self):
		temp = "{:4.1f}".format(rwchcd_Control.ToutdoorGet())
		currmode = rwchcd_Control.SysmodeGet()
		fm = formMode()
		fm.sysmode.value = currmode
		return render.rwchcd(fm, temp)
	def POST(self):
		i = web.input()
		mode = int(i['sysmode'])
		rwchcd_Control.SysmodeSet(mode)
		raise web.found(web.url())
		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
