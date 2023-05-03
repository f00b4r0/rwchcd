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
RWCHCD_DBUS_OBJ_DHWTS = RWCHCD_DBUS_OBJ_BASE + '/plant/dhwts'
RWCHCD_DBUS_OBJ_TEMPS = RWCHCD_DBUS_OBJ_BASE + '/inputs/temperatures'

RWCHCD_DBUS_IFACE_RUNTIME = RWCHCD_DBUS_NAME + '.Runtime'
RWCHCD_DBUS_IFACE_HCIRC = RWCHCD_DBUS_NAME + '.Hcircuit'
RWCHCD_DBUS_IFACE_DHWT = RWCHCD_DBUS_NAME + '.DHWT'
RWCHCD_DBUS_IFACE_TEMP = RWCHCD_DBUS_NAME + '.Temperature'

bus = SystemBus()

# config JSON:
# {
# "dhwts": [0, 1, ...],
# "dhwtrunmodes": [[1, "Mode Général"], [2, "Confort"], [3, "Réduit"], [4, "Hors-Gel"]],
# "hcircuits": [0, 1, ...],
# "modes": [[1, "Off"], [2, "Auto"], [3, "Confort"], [4, "Réduit"], [5, "Hors-Gel"], [6, "ECS"]],	# add 128 for disabling DHW
# "graphurl": "url",
# "homeurl": "url",
# "hcircrunmodes": [[1, "Mode Général"], [2, "Confort"], [3, "Réduit"], [4, "Hors-Gel"]],
# "temperatures": [0, 1, ...],
# "toutdoor": N,
# "tindoor": N,
# "webapptitle": "title"
# }
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
		hcirc = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_HCIRC]
		return hcirc.Name
	if type == "dhwt":
		#this is only called from template and thus cannot trigger an error
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_DHWTS, id)
		dhwt = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_DHWT]
		return dhwt.Name

def gettemp(id):
	try:
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_TEMPS, id)
		temp_T = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_TEMP]
		temp = temp_T.Value
	except:
		temp = float('nan')
	return "{:.1f}".format(temp)

template_globals = {
	'getcfg': getcfg,
	'getobjname': getobjname,
	'app_path': lambda p: web.ctx.homepath + p
}

render = web.template.render('templates/', base='base', globals=template_globals)
tplmanifest = web.template.frender('templates/manifest.json', globals=template_globals)

urls = (
	'/', 'rwchcd',
	'/hcircuit/(\d+)', 'hcircuit',
	'/dhwt/(\d+)', 'dhwt',
	'/temperatures', 'temperatures',
	'/manifest.json', 'manifest',
)

from web import net
class BootForm(form.Form):
	def render_css(self):
		out = []
		out.append(self.rendernote(self.note))
		for i in self.inputs:
			help = i.attrs.pop('help', "")
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
			if help:
				out.append('<div class="form-text">%s</div>' % (net.websafe(help)))
			out.append('</div>\n')
		return ''.join(out)

	def rendernote(self, note):
		return '<div class="invalid-feedback">%s</div>' % net.websafe(note) if note else ''

class BootGrpForm(BootForm):
	def render_css(self):
		out = []
		out.append(self.rendernote(self.note))
		for i in self.inputs:
			if i.note:
				i.attrs['class'] += ' is-invalid'
			out.append(i.pre)
			out.append(i.render())
			out.append(self.rendernote(i.note))
			out.append(i.post)
		return ''.join(out)

# GroupOpen(name, label='label')
class GroupOpen(form.Input):
	def render(self):
		attrs = self.attrs.copy()
		label = attrs.pop("label", None)
		x = '<div class="mb-3">'
		if label:
			x += f'<label class="form-label">{net.websafe(label)}</label>'
		x += '<div class="input-group">'
		return x

# GroupClose(name, help='help')
class GroupClose(form.Input):
	def render(self):
		attrs = self.attrs.copy()
		help = attrs.pop("help", None)
		x = '</div>'
		if help:
			x += f'<div class="form-text">{net.websafe(help)}</div>'
		x += '</div>'
		return x


# NB: https://kzar.co.uk/blog/2010/10/01/web.py-checkboxes

formRwchcd = BootForm(
	form.Dropdown('sysmode', cfg.get('modes'), description='Mode Général', class_='form-select', help='Règle le mode de fonctionnement général de l\'installation'),
	)

formHcTemps = BootForm(
	form.Textbox('overridetemp', form.notnull, form.regexp('^[+-]?\d+\.?\d*$', 'chiffre décimal: (+-)N(.N)'), description='Ajustement (°C)', class_='form-control', help='S\'applique en plus ou en moins (si négatif) des températures de consigne.' ),
	)

formHcRunMode = BootGrpForm(
	GroupOpen('', label='Mode Forcé'),
	form.Checkbox('overriderunmode', value='y', onchange='document.getElementById("runmode").disabled = !this.checked;', pre='<div class="input-group-text">', post='</div>', class_='form-check-input mt-0'),
	form.Dropdown('runmode', cfg.get('hcircrunmodes'), class_='form-select'),
	GroupClose('', help='Cocher la case pour forcer un mode différent du réglage actuel'),
	)

formDhwProps = BootForm(
	form.Checkbox('chargeon', disabled='true', description='Chauffe en cours', value='y', class_='form-check form-switch', role='switch'),
	form.Checkbox('forcecharge', description='Chauffe Forcée', value='y', class_='form-check form-switch', role='switch', help='N\'utiliser que pour test'),
	)


formDhwRunMode = BootGrpForm(
	GroupOpen('', label='Mode Forcé'),
	form.Checkbox('overriderunmode', value='y', onchange='document.getElementById("runmode").disabled = !this.checked;', pre='<div class="input-group-text">', post='</div>', class_='form-check-input mt-0'),
	form.Dropdown('runmode', cfg.get('dhwtrunmodes'), class_='form-select'),
	GroupClose('', help='Cocher la case pour forcer un mode différent du réglage actuel'),
	)

class rwchcd:
	def get_rwchruntime(self):
		return bus.get(RWCHCD_DBUS_NAME, RWCHCD_DBUS_OBJ_BASE)[RWCHCD_DBUS_IFACE_RUNTIME]

	def prep_rwchdata(self):
		data = {}
		data["temps"] = []
		if getcfg('toutdoor') is not None:
			data["temps"].append(("Température Extérieure", gettemp(getcfg('toutdoor'))))
		if getcfg('tindoor') is not None:
			data["temps"].append(("Température Intérieure", gettemp(getcfg('tindoor'))))
		data["forms"] = []
		return data

	def GET(self):
		data = self.prep_rwchdata()
		rwchcd_Runtime = self.get_rwchruntime()
		if cfg.get('modes'):
			currmode = rwchcd_Runtime.SystemMode
			if rwchcd_Runtime.StopDhw:
				currmode |= 0x80

			fr = formRwchcd()
			fr.sysmode.value = currmode
			data["forms"].append(fr)

		return render.rwchcd(data)

	def POST(self):
		if not cfg.get('modes'):
			raise web.badrequest()

		fr = formRwchcd()
		rwchcd_Runtime = self.get_rwchruntime()

		if not fr.validates():
			fr.sysmode.value = int(fr.sysmode.value)
			data = self.prep_rwchdata()
			data["forms"].append(fr)
			return render.rwchcd(data)
		else:
			mode = int(fr.sysmode.value)
			rwchcd_Runtime.StopDhw = mode & 0x80
			rwchcd_Runtime.SystemMode = mode & 0x7F
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			return render.valid(web.ctx.path)


class hcircuit:
	def get_hcirc(self, id):
		try:
			notfound = int(id) not in cfg.get('hcircuits')
		except:
			raise web.badrequest()
		if notfound:
			raise web.notfound()

		#with the above, this "cannot" fail
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
		return bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_HCIRC]

	def prep_hcdata(self, hcirc):
		data = {}
		data["title"] = "Réglages Chauffage"
		data["name"] = hcirc.Name
		data["temps"] = [
			("T° Consigne Confort", "{:.1f}".format(hcirc.TempComfort)),
			("T° Consigne Réduit", "{:.1f}".format(hcirc.TempEco)),
			("T° Consigne Hors-Gel", "{:.1f}".format(hcirc.TempFrostFree)),
			("T° Consigne Actuelle", "{:.1f}".format(hcirc.AmbientRequest)),
		]
		if hcirc.HasAmbientSensor:
			data["temps"].append(("T° Ambiante Actuelle", "{:.1f}".format(hcirc.AmbientActual)))
		data["forms"] = []
		return data

	def GET(self, id):
		hcirc = self.get_hcirc(id)
		data = self.prep_hcdata(hcirc)

		ft = formHcTemps()
		ft.overridetemp.value = "{:.1f}".format(hcirc.TempOffsetOverride)
		data["forms"].append(ft)

		if cfg.get('hcircrunmodes'):
			frm = formHcRunMode()
			frm.overriderunmode.checked = hcirc.RunModeOverride
			if not frm.overriderunmode.checked:
				frm.runmode.attrs["disabled"] = 'true'
			frm.runmode.value = hcirc.RunMode
			data["forms"].append(frm)

		return render.settings(data)

	def POST(self, id):
		hcirc = self.get_hcirc(id)

		ft = formHcTemps()
		vft = ft.validates()

		frm = None
		vfrm = True
		if cfg.get('hcircrunmodes'):
			frm = formHcRunMode()
			vfrm = frm.validates()
			if not frm.overriderunmode.checked:
				frm.runmode.attrs["disabled"] = 'true'

		if not (vft and vfrm):
			data = self.prep_hcdata(hcirc)
			data["forms"].append(ft)
			if frm: data["forms"].append(frm)
			return render.settings(data)
		else:
			hcirc.SetTempOffsetOverride(float(ft.overridetemp.value))
			if cfg.get('hcircrunmodes'):
				if frm.overriderunmode.checked:
					hcirc.SetRunmodeOverride(int(frm.runmode.value))
				else:
					hcirc.DisableRunmodeOverride()
			return render.valid(web.ctx.path)


class dhwt:
	def get_dhwt(self, id):
		try:
			notfound = int(id) not in cfg.get('dhwts')
		except:
			raise web.badrequest()
		if notfound:
			raise web.notfound()

		#with the above, this "cannot" fail
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_DHWTS, id)
		return bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_DHWT]

	def prep_dhwtdata(self, dhwt):
		data = {}
		data["title"] = "Réglages ECS"
		data["name"] = dhwt.Name
		data["temps"] = [
			("T° Consigne Confort", "{:.1f}".format(dhwt.TempComfort)),
			("T° Consigne Réduit", "{:.1f}".format(dhwt.TempEco)),
			("T° Consigne Hors-Gel", "{:.1f}".format(dhwt.TempFrostFree)),
			("T° Consigne Actuelle", "{:.1f}".format(dhwt.TempTarget)),
			("T° Actuelle", "{:.1f}".format(dhwt.TempCurrent)),
		]
		data["forms"] = []
		return data

	def GET(self, id):
		dhwt = self.get_dhwt(id)
		data = self.prep_dhwtdata(dhwt)

		fp = formDhwProps()
		fp.chargeon.checked = dhwt.ChargeOn
		fp.forcecharge.checked = dhwt.ForceChargeOn
		data["forms"].append(fp)

		if cfg.get('dhwtrunmodes'):
			frm = formDhwRunMode()
			frm.overriderunmode.checked = dhwt.RunModeOverride
			if not frm.overriderunmode.checked:
				frm.runmode.attrs["disabled"] = 'true'
			frm.runmode.value = dhwt.RunMode
			data["forms"].append(frm)

		return render.settings(data)

	def POST(self, id):
		dhwt = self.get_dhwt(id)

		fp = formDhwProps()
		vfp = fp.validates()

		frm = None
		vfrm = True
		if cfg.get('dhwtrunmodes'):
			frm = formDhwRunMode()
			vfrm = frm.validates()
			if not frm.overriderunmode.checked:
				frm.runmode.attrs["disabled"] = 'true'

		if not (vfp and vfrm):
			data = self.prep_dhwtdata(dhwt)
			data["forms"].append(fp)
			if frm: data["forms"].append(frm)
			return render.settings(data)
		else:
			dhwt.ForceChargeOn = fp.forcecharge.checked
			if cfg.get('dhwtrunmodes'):
				if frm.overriderunmode.checked:
					dhwt.SetRunmodeOverride(int(frm.runmode.value))
				else:
					dhwt.DisableRunmodeOverride()
			return render.valid(web.ctx.path)


class temperatures:
	def GET(self):
		if not cfg.get('temperatures'):
			raise web.notfound()

		tlist = []

		for id in cfg.get('temperatures'):
			obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_TEMPS, id)
			temp = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_TEMP]

			tlist.append((temp.Name, "{:.1f}".format(temp.Value)))

		return render.temperatures(tlist)


class manifest:
	def GET(self):
		if not cfg.get('webapptitle'):
			raise web.notfound()
		web.header('Content-Type', 'application/manifest+json')
		return tplmanifest()


if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
