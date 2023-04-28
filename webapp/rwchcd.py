#!/usr/bin/python3
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
# "hcircuits": [0, 1, ...],
# "modes": [[1, "Off"], [2, "Auto"], [3, "Confort"], [4, "Eco"], [5, "Hors-Gel"], [6, "ECS"]],	# add 128 for disabling DHW
# "graphurl": "url",
# "toutdoor": N,
# "tindoor": N,
# "webapptitle": "title"
# }
# defining webapptitle enables PWA integration
def loadcfg():
	config = {}
	with open(CFG_FILE, 'r') as f:
		config = json.load(f)
	return config

cfg = loadcfg()

def getcfg(key):
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

template_globals = {
	'getcfg': getcfg,
	'gettemp': gettemp,
	'getobjname': getobjname,
	'app_path': lambda p: web.ctx.homepath + p
}

render = web.template.render('templates/', base='base', globals=template_globals)
tplmanifest = web.template.frender('templates/manifest.json', globals=template_globals)

urls = (
	'/', 'rwchcd',
	'/hcircuit/(\d+)', 'hcircuit',
	'/manifest.json', 'manifest',
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
			out.append('</div>\n')
		return ''.join(out)

	def rendernote(self, note):
		return '<div class="invalid-feedback">%s</div>' % net.websafe(note) if note else ''

class BootGrpForm(BootForm):
	def __init__(self, *inputs, **kw):
		self.grplabel = kw.pop("grplabel", "")
		super().__init__(*inputs, **kw)

	def render_css(self):
		out = []
		out.append(self.rendernote(self.note))
		out.append('<div class="mb-3">')
		if self.grplabel:
			out.append('<label class="form-label">%s</label>' % (net.websafe(self.grplabel)))
		out.append('<div class="input-group">')
		for i in self.inputs:
			if i.note:
				i.attrs['class'] += ' is-invalid'
			out.append(i.pre)
			out.append(i.render())
			out.append(self.rendernote(i.note))
			out.append(i.post)
		out.append('</div></div>\n')
		return ''.join(out)


# NB: https://kzar.co.uk/blog/2010/10/01/web.py-checkboxes

formRwchcd = BootForm(
	form.Dropdown('sysmode', cfg['modes'], description='Mode', class_='form-select'),
	)

formTemps = BootForm(
	form.Textbox('name', disabled='true', description='Nom', class_='form-control'),
	form.Textbox('comftemp', disabled='true', description='Confort', class_='form-control'),
	form.Textbox('econtemp', disabled='true', description='Eco', class_='form-control'),
	form.Textbox('frostemp', disabled='true', description='Hors-Gel', class_='form-control'),
	form.Textbox('overridetemp', form.notnull, form.regexp('^[+-]?\d+\.?\d*$', 'decimal number: (+-)N(.N)'), description='Ajustement', class_='form-control'),
	)

formRunMode = BootGrpForm(
	form.Checkbox('overriderunmode', value='y', pre='<div class="input-group-text">', post='</div>', class_='form-check-input mt-0'),
	form.Dropdown('runmode', [[1, "Auto"], [2, "Confort"], [3, "Eco"], [4, "Hors-Gel"]], class_='form-select'),
	grplabel="Mode Forcé"
	)


class rwchcd:
	def GET(self):
		currmode = rwchcd_Runtime.SystemMode
		if rwchcd_Runtime.StopDhw:
			currmode |= 0x80

		fr = formRwchcd()
		fr.sysmode.value = currmode

		return render.rwchcd(fr)

	def POST(self):
		fr = formRwchcd()

		if not fr.validates():
			fr.sysmode.value = int(fr.sysmode.value)
			return render.rwchcd(form)
		else:
			mode = int(fr.sysmode.value)
			rwchcd_Runtime.StopDhw = mode & 0x80
			rwchcd_Runtime.SystemMode = mode & 0x7F
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			return render.valid(web.ctx.path)


class hcircuit:
	def GET(self, id):
		if int(id) not in cfg.get('hcircuits'):
			raise web.badrequest()
		#with the above, this "cannot" fail
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
		bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
		hcirc = bustemp[RWCHCD_DBUS_IFACE_HCIRC]

		ft = formTemps()
		ft.name.value = hcirc.Name
		ft.comftemp.value = "{:.1f}".format(hcirc.TempComfort)
		ft.econtemp.value = "{:.1f}".format(hcirc.TempEco)
		ft.frostemp.value = "{:.1f}".format(hcirc.TempFrostFree)
		ft.overridetemp.value = "{:.1f}".format(hcirc.TempOffsetOverride)

		frm = formRunMode()
		frm.overriderunmode.checked = hcirc.RunModeOverride
		frm.runmode.value = hcirc.RunMode

		return render.hcircuit(ft, frm)

	def POST(self, id):
		if int(id) not in cfg.get('hcircuits'):
			raise web.badrequest()

		ft = formTemps()
		frm = formRunMode()
		vft = ft.validates()
		vfrm = frm.validates()

		if not (vft and vfrm):
			return render.hcircuit(ft, frm)
		else:
			obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
			bustemp = bus.get(RWCHCD_DBUS_NAME, obj)
			hcirc = bustemp[RWCHCD_DBUS_IFACE_HCIRC]
			hcirc.SetTempOffsetOverride(float(ft.overridetemp.value))
			if frm.overriderunmode.checked:
				hcirc.SetRunmodeOverride(int(frm.runmode.value))
			else:
				hcirc.DisableRunmodeOverride()
			return render.valid(web.ctx.path)


class manifest:
	def GET(self):
		if not cfg.get('webapptitle'):
			raise web.notfound()
		web.header('Content-Type', 'application/manifest+json')
		return tplmanifest()


if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
