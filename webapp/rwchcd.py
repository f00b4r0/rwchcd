#!/usr/bin/python

# License: GPLv2

# TODO: check http://www.web2py.com

import web
import json
from web import form
from pydbus import SystemBus
from os import system

CFG_FILE = 'cfg.json'

RWCHCD_DBUS_NAME = 'org.slashdirt.rwchcd'

RWCHCD_DBUS_OBJ_BASE = '/org/slashdirt/rwchcd'
RWCHCD_DBUS_OBJ_HCIRCS = RWCHCD_DBUS_OBJ_BASE + '/plant/hcircuits'
RWCHCD_DBUS_OBJ_TEMPS = RWCHCD_DBUS_OBJ_BASE + '/inputs/temperatures'

RWCHCD_DBUS_IFACE_RUNTIME = RWCHCD_DBUS_NAME + '.Runtime'
RWCHCD_DBUS_IFACE_HCIRC = RWCHCD_DBUS_NAME + '.Hcircuit'
RWCHCD_DBUS_IFACE_TEMP = RWCHCD_DBUS_NAME + '.Temperature'

bus = SystemBus()
rwchcd = bus.get(RWCHCD_DBUS_NAME, RWCHCD_DBUS_OBJ_BASE)
rwchcd_Runtime = rwchcd[RWCHCD_DBUS_IFACE_RUNTIME]

# XXX REVISIT: I don't know how to walk the object tree with Pydbus
hcircuit0 = bus.get(RWCHCD_DBUS_NAME, RWCHCD_DBUS_OBJ_HCIRCS+'/0')
hcircuit0_Hcircuit = hcircuit0[RWCHCD_DBUS_IFACE_HCIRC]

temp0 = bus.get(RWCHCD_DBUS_NAME, RWCHCD_DBUS_OBJ_TEMPS+'/0')
temp0_Temperature = temp0[RWCHCD_DBUS_IFACE_TEMP]

render = web.template.render('templates/', base='base')

urls = (
	'/', 'rwchcd',
	'/circuit', 'circuit',
)

# config JSON:
# {"modes": [[1, "Off"], [2, "Auto"], [3, "Confort"], [4, "Eco"], [5, "Hors-Gel"], [6, "ECS"]]}
def loadcfg():
	config = {}
	with open(CFG_FILE, 'r') as f:
		config = json.load(f)
	return config

from web import net
class BootForm(form.Form):
	def render_css(self):
		out = []
		out.append(self.rendernote(self.note))
		for i in self.inputs:
			out.append('<div class="mb-3">')
			if i.note:
				i.attrs['class'] += ' is-invalid'
			if not i.is_hidden():
				out.append('<label for="%s" class="form-label">%s</label>' % (i.id, net.websafe(i.description)))
			out.append(i.pre)
			out.append(i.render())
			out.append(self.rendernote(i.note))
			out.append(i.post)
			out.append('</div>')
			out.append('\n')
		return ''.join(out)

	def rendernote(self, note):
		if note: return '<div class="invalid-feedback">%s</div>' % net.websafe(note)
		else: return ""


formMode = BootForm(
	form.Dropdown('sysmode', [], description='Mode', class_='form-select'),
	)

formTemps = BootForm(
	form.Textbox('name', disabled='true', description='Nom', class_='form-control'),
	form.Textbox('comftemp', disabled='true', description='Confort', class_='form-control'),
	form.Textbox('econtemp', disabled='true', description='Eco', class_='form-control'),
	form.Textbox('frostemp', disabled='true', description='Hors-Gel', class_='form-control'),
	form.Textbox('overridetemp', form.notnull, form.regexp('^-?\d+\.?\d*$', 'decimal number: xx.x'), description='Ajustement', class_='form-control'),
	)

class rwchcd:
	def GET(self):
		cfg = loadcfg()
		outtemp = "{:.1f}".format(temp0_Temperature.Value)
		currmode = rwchcd_Runtime.SystemMode
		fm = formMode()
		fm.sysmode.args = cfg['modes']
		fm.sysmode.value = currmode
		return render.rwchcd(fm, outtemp)
	def POST(self):
		cfg = loadcfg()
		temp = 0
		form = formMode()
		form.sysmode.args = cfg['modes']
		if not form.validates():
			form.sysmode.value = int(form.sysmode.value)
			return render.rwchcd(form, temp)
		else:
			mode = int(form.sysmode.value)
			rwchcd_Runtime.SystemMode = mode
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			raise web.found(web.ctx.environ['HTTP_REFERER'])

class circuit:
	def GET(self):
		Name = hcircuit0_Hcircuit.Name
		Comftemp = "{:.1f}".format(hcircuit0_Hcircuit.TempComfort)
		Ecotemp = "{:.1f}".format(hcircuit0_Hcircuit.TempEco)
		Frosttemp = "{:.1f}".format(hcircuit0_Hcircuit.TempFrostFree)
		OffsetOverrideTemp = "{:.1f}".format(hcircuit0_Hcircuit.TempOffsetOverride)
		fm = formTemps()
		fm.name.value = Name
		fm.comftemp.value = Comftemp
		fm.econtemp.value = Ecotemp
		fm.frostemp.value = Frosttemp
		fm.overridetemp.value = OffsetOverrideTemp
		return render.circuit(fm)
	def POST(self):
		form = formTemps()
		if not form.validates():
			return render.circuit(form)
		else:
			overridetemp = float(form.overridetemp.value)
			hcircuit0_Hcircuit.SetTempOffsetOverride(overridetemp)
			raise web.found(web.ctx.environ['HTTP_REFERER'])
		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
