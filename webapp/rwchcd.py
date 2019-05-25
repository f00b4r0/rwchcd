#!/usr/bin/python

# License: GPLv2

# TODO: check http://www.web2py.com

import web
from web import form
from pydbus import SystemBus
from os import system

bus = SystemBus()
rwchcd = bus.get('org.slashdirt.rwchcd', '/org/slashdirt/rwchcd')
rwchcd_Control = rwchcd['org.slashdirt.rwchcd.Control']

render = web.template.render('templates/')

urls = (
	'/', 'rwchcd',
	'/temps', 'temps',
	'/outhoffs', 'outhoffs',
)

formMode = form.Form(
	form.Dropdown('sysmode', [(1, 'Off'), (2, 'Auto'), (3, 'Confort'), (4, 'Eco'), (5, 'Hors-Gel'), (6, 'ECS')], description='Mode'),
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
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			raise web.found('')

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
			raise web.found('')

class outhoffs:
	def GET(self):
		Comfouthoff = "{:.1f}".format(rwchcd_Control.ConfigOuthoffModeGet(2))
		Ecoouthoff = "{:.1f}".format(rwchcd_Control.ConfigOuthoffModeGet(3))
		Frostouthoff = "{:.1f}".format(rwchcd_Control.ConfigOuthoffModeGet(4))
		fm = formTemps()
		fm.comftemp.value = Comfouthoff
		fm.econtemp.value = Ecoouthoff
		fm.frostemp.value = Frostouthoff
		return render.outhoffs(fm)
	def POST(self):
		form = formTemps()
		if not form.validates():
			return render.outhoffs(form)
		else:
			comftemp = float(form.comftemp.value)
			econtemp = float(form.econtemp.value)
			frostemp = float(form.frostemp.value)
			rwchcd_Control.ConfigOuthoffModeSet(2, comftemp)
			rwchcd_Control.ConfigOuthoffModeSet(3, econtemp)
			rwchcd_Control.ConfigOuthoffModeSet(4, frostemp)
			raise web.found('')

		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
