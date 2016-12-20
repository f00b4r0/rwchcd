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
	form.Dropdown('sysmode', [(0, 'Off'), (1, 'Auto'), (2, 'Confort'), (3, 'Eco'), (4, 'Hors-Gel'), (5, 'ECS')]),
	form.Textbox('systemp', form.regexp('^\d+\.?\d*$', 'decimal number: xx.x')),
	form.Checkbox('chgtmp', value='yes'),
	)

class rwchcd:
	def GET(self):
		outtemp = "{:.1f}".format(rwchcd_Control.ToutdoorGet())
		currmode = rwchcd_Control.SysmodeGet()
		if (currmode == 1):
			currtemp = "0.0"
		else:
			currtemp = "{:.1f}".format(rwchcd_Control.ConfigTempGet())
		fm = formMode()
		fm.sysmode.value = currmode
		fm.systemp.value = currtemp
		return render.rwchcd(fm, outtemp)
	def POST(self):
		temp = 0
		form = formMode()
		if not form.validates():
			form.sysmode.value = int(form.sysmode.value)
			return render.rwchcd(form, temp)
		else:
			if not form.chgtmp.checked:
				mode = int(form.sysmode.value)
				rwchcd_Control.SysmodeSet(mode)
			else:
				newtemp = float(form.systemp.value)
				rwchcd_Control.ConfigTempSet(newtemp)
			raise web.found(web.url())
		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
