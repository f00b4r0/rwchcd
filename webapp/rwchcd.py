#!/usr/bin/python
# -*- coding: utf-8 -*-

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

# config JSON:
# {
# "allowstopdhw": 1,
# "hcircuits": [0, 1, ...],
# "modes": [[1, "Off"], [2, "Auto"], [3, "Confort"], [4, "Eco"], [5, "Hors-Gel"], [6, "ECS"]],
# "graphurl": "url",
# "toutdoor": N,
# "tindoor": N,
# "webapptitle": "title"
# }
def loadcfg():
	config = {}
	with open(CFG_FILE, 'r') as f:
		config = json.load(f)
	return config

def getcfg(key):
	cfg = loadcfg()
	return cfg.get(key)

def getobjname(type, id):
	if type == "hcircuit":
		#this is only called from template and thus cannot trigger an error
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
		bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
		hcirc = bustemp[RWCHCD_DBUS_IFACE_HCIRC]
		return hcirc.Name

def gettemp(id):
	try:
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_TEMPS, id)
		bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
		temp_T = bustemp[RWCHCD_DBUS_IFACE_TEMP]
		temp = temp_T.Value
	except:
		temp = float('nan')
	return "{:.1f}".format(temp)

render = web.template.render('templates/', base='base', globals={'getcfg': getcfg, 'gettemp': gettemp, 'getobjname': getobjname})

urls = (
	'/', 'rwchcd',
	'/hcircuit/(\d+)', 'hcircuit',
)

from web import net
class BootForm(form.Form):
	def render_css(self):
		out = []
		out.append(self.rendernote(self.note))
		for i in self.inputs:
			out.append('<div class="mb-3">')
			if isinstance(i, form.Checkbox):
				out.append('<div class="%s">' % i.attrs['class'])
				i.attrs['class'] = 'form-check-input'
				out.append('<label for="%s" class="form-check-label">%s</label>' % (net.websafe(i.id), net.websafe(i.description)))
			else:
				if i.note:
					i.attrs['class'] += ' is-invalid'
				if not i.is_hidden():
					out.append('<label for="%s" class="form-label">%s</label>' % (net.websafe(i.id), net.websafe(i.description)))
			out.append(i.pre)
			out.append(i.render())
			out.append(self.rendernote(i.note))
			out.append(i.post)
			if isinstance(i, form.Checkbox):
				out.append('</div>')
			out.append('</div>')
			out.append('\n')
		return ''.join(out)

	def rendernote(self, note):
		if note: return '<div class="invalid-feedback">%s</div>' % net.websafe(note)
		else: return ""

def makerwchcdform(cfg):
	args = (form.Dropdown('sysmode', [], description='Mode', class_='form-select'), )
	if cfg.get('allowstopdhw'):
		args = args + (form.Checkbox('stopdhw', description='Arrêt ECS', value='dummy', class_='form-check'), )
	return BootForm(*args)

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
		currmode = rwchcd_Runtime.SystemMode
		fm = makerwchcdform(cfg)
		fm.sysmode.args = cfg['modes']
		fm.sysmode.value = currmode
		if cfg.get('allowstopdhw'):
			fm.stopdhw.checked = rwchcd_Runtime.StopDhw
		return render.rwchcd(fm)
	def POST(self):
		cfg = loadcfg()
		form = makerwchcdform(cfg)
		form.sysmode.args = cfg['modes']
		if not form.validates():
			form.sysmode.value = int(form.sysmode.value)
			return render.rwchcd(form)
		else:
			mode = int(form.sysmode.value)
			rwchcd_Runtime.SystemMode = mode
			if cfg.get('allowstopdhw'):
				rwchcd_Runtime.StopDhw = form.stopdhw.checked
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			raise web.found(web.ctx.environ['HTTP_REFERER'])

class hcircuit:
	def GET(self, id):
		cfg = loadcfg()
		if int(id) not in cfg.get('hcircuits'):
			raise web.badrequest()
		#with the above, this "cannot" fail
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
		bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
		hcirc = bustemp[RWCHCD_DBUS_IFACE_HCIRC]
		Name = hcirc.Name
		Comftemp = "{:.1f}".format(hcirc.TempComfort)
		Ecotemp = "{:.1f}".format(hcirc.TempEco)
		Frosttemp = "{:.1f}".format(hcirc.TempFrostFree)
		OffsetOverrideTemp = "{:.1f}".format(hcirc.TempOffsetOverride)
		fm = formTemps()
		fm.name.value = Name
		fm.comftemp.value = Comftemp
		fm.econtemp.value = Ecotemp
		fm.frostemp.value = Frosttemp
		fm.overridetemp.value = OffsetOverrideTemp
		return render.hcircuit(fm)
	def POST(self, id):
		cfg = loadcfg()
		if int(id) not in cfg.get('hcircuits'):
			raise web.badrequest()
		form = formTemps()
		if not form.validates():
			return render.hcircuit(form)
		else:
			obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
			bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
			hcirc = bustemp[RWCHCD_DBUS_IFACE_HCIRC]
			overridetemp = float(form.overridetemp.value)
			hcirc.SetTempOffsetOverride(overridetemp)
			raise web.found(web.ctx.environ['HTTP_REFERER'])
		

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
