#!/usr/bin/python3
# -*- coding: utf-8 -*-

# License: GPLv2

# TODO: check http://www.web2py.com

import web
import json
from web import form
from pydbus import SystemBus
from os import system
from time import sleep
import paho.mqtt.publish as publish

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
# "goddhwts: [0, 1, ...],
# "dhwtrunmodes": [[1, "Mode Général"], [4, "Hors-Gel"]],
# "dhwtgodmodes": [[1, "Mode Général"], [2, "Confort"], [3, "Réduit"], [4, "Hors-Gel"]],
# "hcircuits": [0, 1, ...],
# "godhcircuits:" [0, 1, ...],
# "modes": [[2, "Auto"], [4, "Réduit"], [5, "Hors-Gel"], [6, "ECS"]],			# add 128 for disabling DHW
# "godmodes": [[1, "Off"], [2, "Auto"], [3, "Confort"], [4, "Réduit"], [5, "Hors-Gel"], [6, "ECS"], [7, "Test"]],	# add 128 for disabling DHW
# "godname": "admin",	# login name of administrator, with access to "god" variables
# "graphurl": "url",
# "homeurl": "url",
# "hcircrunmodes": [[1, "Mode Général"], [4, "Hors-Gel"]],
# "hcircgodmodes": [[1, "Mode Général"], [2, "Confort"], [3, "Réduit"], [4, "Hors-Gel"]],
# "temperatures": [0, 1, ...],
# "godtemperatures": [0, 1, ...],
# "toutdoor": N,
# "tindoor": N,
# "webapptitle": "title",
# "mqtthost": "hostname",
# "mqtttopicbase: "rwchcd"
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

def get_godalt(cfggod, cfgdef):
	god = cfg.get('godname') and (web.ctx.env.get('REMOTE_USER') == cfg.get('godname'))
	if god:
		alt = cfg.get(cfggod) or cfg.get(cfgdef)
	else:
		alt = cfg.get(cfgdef)
	return alt

def ftemp(t):
	if t == -273:
		return "--.-"
	else:
		return "{:.1f}".format(t)

def gettemp(id):
	try:
		obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_TEMPS, id)
		temp_T = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_TEMP]
		temp = temp_T.Value
	except:
		temp = float('nan')
	return ftemp(temp)

template_globals = {
	'getcfg': getcfg,
	'getobjname': getobjname,
	'get_godalt': get_godalt,
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
	form.Dropdown('sysmode', None, description='Mode Général', class_='form-select', help='Règle le mode de fonctionnement général de l\'installation'),
	)

formHcTemps = BootForm(
	form.Textbox('overridetemp', form.notnull, form.regexp('^[+-]?\d+\.?\d*$', 'chiffre décimal: (+-)N(.N)'), description='Ajustement (°C)', class_='form-control', help='S\'applique en plus ou en moins (si négatif) des températures de consigne.' ),
	)

formHcRunMode = BootGrpForm(
	GroupOpen('', label='Mode Actuel'),
	form.Checkbox('overriderunmode', value='y', onchange='document.getElementById("runmode").disabled = !this.checked;', pre='<div class="input-group-text">', post='</div>', class_='form-check-input mt-0'),
	form.Dropdown('runmode', None, class_='form-select'),
	GroupClose('', help='Cocher la case pour choisir et forcer un mode différent du réglage standard, décocher pour y revenir'),
	)

formDhwProps = BootForm(
	form.Checkbox('electricon', disabled='true', description='Fonctionnement sur résistance de chauffe (si disponible)', value='y', class_='form-check form-switch', role='switch'),
	form.Checkbox('chargeon', disabled='true', description='Chauffe en cours', value='y', class_='form-check form-switch', role='switch'),
	form.Checkbox('forcecharge', description='Chauffe Forcée', value='y', class_='form-check form-switch', role='switch', help='N\'utiliser que pour test'),
	)


formDhwRunMode = BootGrpForm(
	GroupOpen('', label='Mode Actuel'),
	form.Checkbox('overriderunmode', value='y', onchange='document.getElementById("runmode").disabled = !this.checked;', pre='<div class="input-group-text">', post='</div>', class_='form-check-input mt-0'),
	form.Dropdown('runmode', None, class_='form-select'),
	GroupClose('', help='Cocher la case pour choisir et forcer un mode différent du réglage standard, décocher pour y revenir'),
	)

def mqtt_pub_mode(mode):
	host = cfg.get('mqtthost')
	if not host:
		return

	topicbase = cfg.get('mqtttopicbase')
	publish.single(topicbase + "/system", payload=mode, retain=True, hostname=host)

def match_runmode(mode, modelist):
	for m in modelist:
		if m[0] == mode:
			return m[1]

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

		data["hcircuits"] = []
		if cfg.get('hcircuits') and cfg.get('hcircrunmodes'):
			for id in cfg.get('hcircuits'):
				obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_HCIRCS, id)
				hcirc = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_HCIRC]
				data["hcircuits"].append((hcirc.Name, match_runmode(hcirc.RunMode, cfg.get('hcircrunmodes')), hcirc.RunModeOverride))

		data["dhwts"] = []
		if cfg.get('dhwts') and cfg.get('dhwtrunmodes'):
			for id in cfg.get('dhwts'):
				obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_DHWTS, id)
				dhwt = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_DHWT]
				data["dhwts"].append((dhwt.Name, match_runmode(dhwt.RunMode, cfg.get('dhwtrunmodes')), dhwt.RunModeOverride))

		data["forms"] = []
		return data

	def GET(self):
		data = self.prep_rwchdata()
		rwchcd_Runtime = self.get_rwchruntime()
		modes = get_godalt('godmodes', 'modes')
		if modes:
			currmode = rwchcd_Runtime.SystemMode
			if rwchcd_Runtime.StopDhw:
				currmode |= 0x80

			fr = formRwchcd()
			fr.sysmode.args = modes
			fr.sysmode.value = currmode
			data["forms"].append(fr)

		return render.rwchcd(data)

	def POST(self):
		modes = get_godalt('godmodes', 'modes')
		if not modes:
			raise web.badrequest()

		fr = formRwchcd()
		fr.sysmode.args = modes
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
			mqtt_pub_mode(mode)
			system("/usr/bin/sudo /sbin/fh-sync >/dev/null 2>&1")	# XXX dirty hack
			return render.valid(web.ctx.path)


class hcircuit:
	def get_hcirc(self, id):
		try:
			notfound = int(id) not in get_godalt('godhcircuits', 'hcircuits')
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
			("T° Consigne Confort", ftemp(hcirc.TempComfort)),
			("T° Consigne Réduit", ftemp(hcirc.TempEco)),
			("T° Consigne Hors-Gel", ftemp(hcirc.TempFrostFree)),
			("T° Consigne Actuelle", ftemp(hcirc.AmbientRequest)),
		]
		if hcirc.HasAmbientSensor:
			data["temps"].append(("T° Ambiante Actuelle", ftemp(hcirc.AmbientActual)))
		data["settings"] = []
		runmodes = get_godalt('hcircgodmodes', 'hcircrunmodes')
		if runmodes:
			data["settings"].append(("Réglage standard", match_runmode(hcirc.RunModeOrig, runmodes)))
		data["forms"] = []
		return data

	def GET(self, id):
		hcirc = self.get_hcirc(id)
		data = self.prep_hcdata(hcirc)

		ft = formHcTemps()
		ft.overridetemp.value = ftemp(hcirc.TempOffsetOverride)
		data["forms"].append(ft)

		runmodes = get_godalt('hcircgodmodes', 'hcircrunmodes')
		if runmodes:
			frm = formHcRunMode()
			frm.overriderunmode.checked = hcirc.RunModeOverride
			if not frm.overriderunmode.checked:
				frm.runmode.attrs["disabled"] = 'true'
			frm.runmode.args = runmodes
			frm.runmode.value = hcirc.RunMode
			data["forms"].append(frm)

		return render.settings(data)

	def POST(self, id):
		hcirc = self.get_hcirc(id)

		ft = formHcTemps()
		vft = ft.validates()

		frm = None
		vfrm = True
		runmodes = get_godalt('hcircgodmodes', 'hcircrunmodes')
		if runmodes:
			frm = formHcRunMode()
			frm.runmode.args = runmodes
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
			if runmodes:
				if frm.overriderunmode.checked and (int(frm.runmode.value) != hcirc.RunModeOrig):
					hcirc.SetRunmodeOverride(int(frm.runmode.value))
				else:
					hcirc.DisableRunmodeOverride()
			return render.valid(web.ctx.path)


class dhwt:
	def get_dhwt(self, id):
		try:
			notfound = int(id) not in get_godalt('goddhwts', 'dhwts')
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
			("T° Consigne Confort", ftemp(dhwt.TempComfort)),
			("T° Consigne Réduit", ftemp(dhwt.TempEco)),
			("T° Consigne Hors-Gel", ftemp(dhwt.TempFrostFree)),
			("T° Consigne Actuelle", ftemp(dhwt.TempTarget)),
			("T° Actuelle", ftemp(dhwt.TempCurrent)),
		]
		data["settings"] = []
		runmodes = get_godalt('dhwtgodmodes', 'dhwtrunmodes')
		if runmodes:
			data["settings"].append(("Réglage standard", match_runmode(dhwt.RunModeOrig, runmodes)))
		data["forms"] = []
		return data

	def GET(self, id):
		dhwt = self.get_dhwt(id)
		data = self.prep_dhwtdata(dhwt)

		fp = formDhwProps()
		fp.chargeon.checked = dhwt.ChargeOn
		fp.electricon.checked = dhwt.ElectricModeOn
		fp.forcecharge.checked = dhwt.ForceChargeOn
		data["forms"].append(fp)

		runmodes = get_godalt('dhwtgodmodes', 'dhwtrunmodes')
		if runmodes:
			frm = formDhwRunMode()
			frm.runmode.args = runmodes
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
		runmodes = get_godalt('dhwtgodmodes', 'dhwtrunmodes')
		if runmodes:
			frm = formDhwRunMode()
			frm.runmode.args = runmodes
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
			if runmodes:
				if frm.overriderunmode.checked and (int(frm.runmode.value) != dhwt.RunModeOrig):
					dhwt.SetRunmodeOverride(int(frm.runmode.value))
				else:
					dhwt.DisableRunmodeOverride()
			return render.valid(web.ctx.path)


class temperatures:
	def GET(self):
		temperatures = get_godalt('godtemperatures', 'temperatures')
		if not temperatures:
			raise web.notfound()

		tlist = []

		for id in temperatures:
			obj = "{0}/{1}".format(RWCHCD_DBUS_OBJ_TEMPS, id)
			temp = bus.get(RWCHCD_DBUS_NAME, obj)[RWCHCD_DBUS_IFACE_TEMP]

			tlist.append((temp.Name, ftemp(temp.Value)))

		return render.temperatures(tlist)


class manifest:
	def GET(self):
		if not cfg.get('webapptitle'):
			raise web.notfound()
		web.header('Content-Type', 'application/manifest+json')
		return tplmanifest()


# publish current state at startup
def startup():
	if cfg.get('modes'):
		while True:
			try:
				rwchcd_Runtime = bus.get(RWCHCD_DBUS_NAME, RWCHCD_DBUS_OBJ_BASE)[RWCHCD_DBUS_IFACE_RUNTIME]
			except:	# rwchcd may not be started yet
				sleep(1)
			else:
				break
		currmode = rwchcd_Runtime.SystemMode
		if rwchcd_Runtime.StopDhw:
			currmode |= 0x80
		mqtt_pub_mode(currmode)

startup()

if __name__ == "__main__":
	app = web.application(urls, globals())
	app.run()
	mqtt_pub_mode(0)
