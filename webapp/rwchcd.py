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
	'/', 'rwchcd',
	'/temps', 'temps',
)

formMode = form.Form(
	form.Dropdown('sysmode', [(0, 'Off'), (1, 'Auto'), (2, 'Confort'), (3, 'Eco'), (4, 'Hors-Gel'), (5, 'ECS')]),
	)

formTemps = form.Form(
	form.Textbox('comftemp', form.notnull, form.regexp('^\d+\.?\d*$', 'decimal number: xx.x'), description='Confort'),
	form.Textbox('econtemp', form.notnull, form.regexp('^\d+\.?\d*$', 'decimal number: xx.x'), description='Eco'),
	form.Textbox('frostemp', form.notnull, form.regexp('^\d+\.?\d*$', 'decimal number: xx.x'), description='Hors-Gel'),
	)

class rwchcd:
	def GET(self):
		outtemp = "{:.1f}".format(rwchcd_Control.ToutdoorGet())
		currmode = rwchcd_Control.SysmodeGet()
		fm = formMode()
		fm.sysmode.value = currmode
		return render.rwchcd(fm, outtemp)
	def POST(self):
		temp = 0
		form = formMode()
		if not form.validates():
			form.sysmode.value = int(form.sysmode.value)
			return render.rwchcd(form, temp)
		else:
			mode = int(form.sysmode.value)
			rwchcd_Control.SysmodeSet(mode)
			raise web.found(web.url())

class temps:
	def GET(self):
		Comftemp = "{:.1f}".format(rwchcd_Control.ConfigTempModeGet(2))
		Ecotemp = "{:.1f}".format(rwchcd_Control.ConfigTempModeGet(3))
		Frosttemp = "{:.1f}".format(rwchcd_Control.ConfigTempModeGet(4))
		fm = formTemps()
		fm.comftemp.value = Comftemp
		fm.econtemp.value = Ecotemp
		fm.frostemp.value = Frosttemp
		return render.temps(fm)
	def POST(self):
		form = formTemps()
		if not form.validates():
			return render.temps(form)
		else:
			comftemp = float(form.comftemp.value)
			econtemp = float(form.econtemp.value)
			frostemp = float(form.frostemp.value)
			rwchcd_Control.ConfigTempModeSet(2, comftemp)
			rwchcd_Control.ConfigTempModeSet(3, econtemp)
			rwchcd_Control.ConfigTempModeSet(4, frostemp)
			raise web.found(web.url())

		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
