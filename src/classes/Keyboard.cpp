/*
  This file is part of g810-led.

  g810-led is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, version 3 of the License.

  g810-led is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with g810-led.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "Keyboard.h"

#include <iostream>
#include <unistd.h>
#include <vector>
#include <map>
#include <cerrno>

#if defined(hidapi)
	#include <locale>
	#include "hidapi/hidapi.h"
#elif defined(libusb)
	#include "libusb-1.0/libusb.h"
#endif


using namespace std;



LedKeyboard::~LedKeyboard() {
	close();
}


vector<LedKeyboard::DeviceInfo> LedKeyboard::listKeyboards() {
	vector<LedKeyboard::DeviceInfo> deviceList;

	#if defined(hidapi)
		if (hid_init() < 0) return deviceList;
		
		struct hid_device_info *devs, *dev;
		devs = hid_enumerate(0x0, 0x0);
		dev = devs;
		while (dev) {
			for (size_t i = 0; i < SupportedKeyboards.size(); i++) {
				if (dev->vendor_id == SupportedKeyboards[i][0]) {
					if (dev->product_id == SupportedKeyboards[i][1]) {
						DeviceInfo deviceInfo;
						deviceInfo.vendorID=dev->vendor_id;
						deviceInfo.productID=dev->product_id;

						if (dev->serial_number != NULL) {
							char buf[256];
							wcstombs(buf, dev->serial_number, 256);
							deviceInfo.serialNumber = string(buf);
						}

						if (dev->manufacturer_string != NULL)
						{
							char buf[256];
							wcstombs(buf, dev->manufacturer_string, 256);
							deviceInfo.manufacturer = string(buf);
						}

						if (dev->product_string != NULL)
						{
							char buf[256];
							wcstombs(buf, dev->product_string, 256);
							deviceInfo.product = string(buf);
						}

						deviceList.push_back(deviceInfo);
						dev = dev->next;
						break;
					}
				}
			}
			if (dev != NULL) dev = dev->next;
		}
		hid_free_enumeration(devs);
		hid_exit();
		
	#elif defined(libusb)
		libusb_context *ctx = NULL;
		if(libusb_init(&m_ctx) < 0) return deviceList;
		
		libusb_device **devs;
		ssize_t cnt = libusb_get_device_list(ctx, &devs);
		for(ssize_t i = 0; i < cnt; i++) {
			libusb_device *device = devs[i];
			libusb_device_descriptor desc;
			libusb_get_device_descriptor(device, &desc);
			for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
				if (desc.idVendor == SupportedKeyboards[i][0]) {
					if (desc.idProduct == SupportedKeyboards[i][1]) {
					  unsigned char buf[256];
						DeviceInfo deviceInfo;
						deviceInfo.vendorID=desc.idVendor;
						deviceInfo.productID=desc.idProduct;

						if (libusb_open(device, &m_hidHandle) != 0)	continue;

						if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iSerialNumber, buf, 256) >= 1) deviceInfo.serialNumber = string((char*)buf);
						if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iManufacturer, buf, 256) >= 1) deviceInfo.manufacturer = string((char*)buf);
						if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iProduct, buf, 256) >= 1) deviceInfo.product = string((char*)buf);

						deviceList.push_back(deviceInfo);
						libusb_close(m_hidHandle);
						m_hidHandle = NULL;
						break;
					}
				}
			}
		}
		libusb_free_device_list(devs, 1);

		if (m_hidHandle != NULL) {
			libusb_close(m_hidHandle);
			m_hidHandle = NULL;
		}
		
		libusb_exit(m_ctx);
		m_ctx = NULL;
	#endif
	
	return deviceList;
}


bool LedKeyboard::isOpen() {
	return m_isOpen;
}

bool LedKeyboard::open() {
	if (m_isOpen) return true;
	
	return open(0x0, 0x0, "");
}

bool LedKeyboard::open(uint16_t vendorID, uint16_t productID, string serial) {
	if (m_isOpen && ! close()) return false;
	currentDevice.model = KeyboardModel::unknown;

	#if defined(hidapi)
		if (hid_init() < 0) return false;

		struct hid_device_info *devs, *dev;
		devs = hid_enumerate(vendorID, productID);
		dev = devs;
		wstring wideSerial;

		if (!serial.empty()) {
			wchar_t tempSerial[256];
			if (mbstowcs(tempSerial, serial.c_str(), 256) < 1) return false;
			wideSerial = wstring(tempSerial);
		}

		while (dev) {
			for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
				if (dev->vendor_id == SupportedKeyboards[i][0] && dev->product_id == SupportedKeyboards[i][1] && dev->interface_number == SupportedKeyboards[i][2]) {
					if (!serial.empty() && dev->serial_number != NULL && wideSerial.compare(dev->serial_number) != 0) break; //Serial didn't match

					if (dev->serial_number != NULL) {
						char buf[256];
						wcstombs(buf,dev->serial_number,256);
						currentDevice.serialNumber=string(buf);
					}

					if (dev->manufacturer_string != NULL)
					{
						char buf[256];
						wcstombs(buf,dev->manufacturer_string,256);
						currentDevice.manufacturer = string(buf);
					}

					if (dev->product_string != NULL)
					{
						char buf[256];
						wcstombs(buf,dev->product_string,256);
						currentDevice.product = string(buf);
					}

					currentDevice.vendorID = dev->vendor_id;
					currentDevice.productID = dev->product_id;
					currentDevice.path = dev->path;
					currentDevice.model = (KeyboardModel)SupportedKeyboards[i][3];
					break;
				}
			}
			if (currentDevice.model != KeyboardModel::unknown) break;
			dev = dev->next;
		}

		hid_free_enumeration(devs);

		if (! dev) {
			currentDevice.model = KeyboardModel::unknown;
			errno = ENODEV;

			hid_exit();
			return false;
		}

		m_hidHandle = hid_open_path(currentDevice.path.c_str());

		if(m_hidHandle == 0) {
			hid_exit();
			errno = EACCES;
			return false;
		}

		m_isOpen = true;
		return true;

	#elif defined(libusb)
		if (libusb_init(&m_ctx) < 0) return false;
			
		libusb_device **devs;
		libusb_device *dev = NULL;
		ssize_t cnt = libusb_get_device_list(m_ctx, &devs);
		if(cnt >= 0) {
			for(ssize_t i = 0; i < cnt; i++) {
				libusb_device *device = devs[i];
				libusb_device_descriptor desc;
				libusb_get_device_descriptor(device, &desc);

				if (vendorID != 0x0 && desc.idVendor != vendorID) continue;
				else if (productID != 0x0 && desc.idProduct != productID) continue;
				else if (! serial.empty()) {
					if (desc.iSerialNumber <= 0) continue; //Device does not populate serial number

					unsigned char buf[256];
					if (libusb_open(device, &m_hidHandle) != 0){
						m_hidHandle = NULL;
						continue;
					}

					if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iSerialNumber, buf, 256) >= 1 && serial.compare((char*)buf) == 0) {
						//Make sure entry is a supported keyboard and get model
						for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
							if (desc.idVendor == SupportedKeyboards[i][0]) {
								if (desc.idProduct == SupportedKeyboards[i][1]) {
									if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iManufacturer, buf, 256) >= 1) currentDevice.manufacturer = string((char*)buf);
									if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iProduct, buf, 256) >= 1) currentDevice.product = string((char*)buf);
									currentDevice.serialNumber = serial;
									currentDevice.vendorID = desc.idVendor;
									currentDevice.productID = desc.idProduct;
									currentDevice.model = (KeyboardModel)SupportedKeyboards[i][3];

									dev = device;
									libusb_close(m_hidHandle);
									m_hidHandle = NULL;
									break;
								}
							}
						}
					}
					else {
						libusb_close(m_hidHandle);
						m_hidHandle = NULL;
						continue; //Serial number set but doesn't match
					}
				}

				//For the case where serial is not specified, find first supported device
				for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
					if (desc.idVendor == SupportedKeyboards[i][0]) {
						if (desc.idProduct == SupportedKeyboards[i][1]) {
							unsigned char buf[256];
							if (libusb_open(device, &m_hidHandle) != 0){
								m_hidHandle = NULL;
								continue;
							}
							currentDevice.vendorID = desc.idVendor;
							currentDevice.productID = desc.idProduct;
							currentDevice.model = (KeyboardModel)SupportedKeyboards[i][3];
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iManufacturer, buf, 256) >= 1) currentDevice.manufacturer = string((char*)buf);
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iProduct, buf, 256) >= 1) currentDevice.product = string((char*)buf);
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iSerialNumber, buf, 256) >= 1) currentDevice.serialNumber = string((char*)buf);

							libusb_close(m_hidHandle);
							m_hidHandle=NULL;
							break;
						}
					}
				}
				if (currentDevice.model != KeyboardModel::unknown) break;
			}
			libusb_free_device_list(devs, 1);
		}

		if (currentDevice.model == KeyboardModel::unknown) {
			libusb_exit(m_ctx);
			errno = ENODEV;
			m_ctx = NULL;
			return false;
		}
			
		if (dev == NULL) m_hidHandle = libusb_open_device_with_vid_pid(m_ctx, currentDevice.vendorID, currentDevice.productID);
		else libusb_open(dev, &m_hidHandle);

		if(m_hidHandle == NULL) {
			libusb_exit(m_ctx);
			errno = EACCES;
			m_ctx = NULL;
			return false;
		}

		int interface_num;
		switch (currentDevice.model) {
			case KeyboardModel::g915:
				interface_num = 2;
				break;
			default:
				interface_num = 1;
		}

		if(libusb_kernel_driver_active(m_hidHandle, interface_num) == 1) {
			if(libusb_detach_kernel_driver(m_hidHandle, interface_num) != 0) {
				libusb_exit(m_ctx);
				errno = EACCES;
				m_ctx = NULL;
				return false;
			}
			m_isKernellDetached = true;
		}
			
		if(libusb_claim_interface(m_hidHandle, interface_num) < 0) {
			if(m_isKernellDetached==true) {
				libusb_attach_kernel_driver(m_hidHandle, interface_num);
				m_isKernellDetached = false;
			}
			libusb_exit(m_ctx);
			errno = EACCES;
			m_ctx = NULL;
			return false;
		}
			
		m_isOpen = true;
		return true;
	#endif

	return false; //In case neither is defined
}

LedKeyboard::DeviceInfo LedKeyboard::getCurrentDevice() {
	return currentDevice;
}

bool LedKeyboard::close() {
	if (! m_isOpen) return true;
	m_isOpen = false;
	
	#if defined(hidapi)
		hid_close(m_hidHandle);
		m_hidHandle = NULL;
		hid_exit();
		return true;
	#elif defined(libusb)
		if (m_hidHandle == NULL) return true;
		if(libusb_release_interface(m_hidHandle, 1) != 0) return false;
		if(m_isKernellDetached==true) {
			libusb_attach_kernel_driver(m_hidHandle, 1);
			m_isKernellDetached = false;
		}
		libusb_close(m_hidHandle);
		m_hidHandle = NULL;
		libusb_exit(m_ctx);
		m_ctx = NULL;
		return true;
	#endif
	
	return false;
}


LedKeyboard::KeyboardModel LedKeyboard::getKeyboardModel() {
	return currentDevice.model;
}

bool LedKeyboard::commit() {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
			return true; // Keyboard is non-transactional
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			data = { 0x11, 0xff, 0x0c, 0x5a };
			break;
		case KeyboardModel::g815:
			data = { 0x11, 0xff, 0x10, 0x7f };
			break;
		case KeyboardModel::g910:
			data = { 0x11, 0xff, 0x0f, 0x5d };
			break;
		case KeyboardModel::g915:
			data = { 0x11, 0x01, 0x0b, 0x7f };
			break;
		default:
			return false;
	}
	data.resize(20, 0x00);
	return sendDataInternal(data);
}

bool LedKeyboard::setKey(LedKeyboard::KeyValue keyValue) {
	return setKeys(KeyValueArray {keyValue});
}

bool LedKeyboard::setKeys(KeyValueArray keyValues) {
	if (keyValues.empty()) return false;
	
	bool retval = true;
	
	vector<vector<KeyValue>> SortedKeys;
	map<int32_t, vector<KeyValue>> KeyByColors;
	map<int32_t, vector<KeyValue>>::iterator KeyByColorsIterator;
	const uint8_t maxKeyPerColor = 13;
	
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x0b;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x10;
			}
			for (uint8_t i = 0; i < keyValues.size(); i++) {
				uint32_t colorkey = static_cast<uint32_t>(keyValues[i].color.red | keyValues[i].color.green << 8 | keyValues[i].color.blue << 16 );
				if (KeyByColors.count(colorkey) == 0) KeyByColors.insert(pair<uint32_t, vector<KeyValue>>(colorkey, {}));
				KeyByColors[colorkey].push_back(keyValues[i]);
			}
			
			for (auto& x: KeyByColors) {
				if (x.second.size() > 0) {
					uint8_t gi = 0;
					while (gi < x.second.size()) {
						size_t data_size = 20;
						byte_buffer_t data = { 0x11, g815_target, g815_feat_idx, 0x6c };
						data.push_back(x.second[0].color.red);
						data.push_back(x.second[0].color.green);
						data.push_back(x.second[0].color.blue);
						for (uint8_t i = 0; i < maxKeyPerColor; i++) {
							if (gi + i < x.second.size()) {
								switch (x.second[gi+i].key) {
									case Key::logo2:
									case Key::game:
									case Key::caps:
									case Key::scroll:
									case Key::num:
									case Key::stop:
									case Key::g6:
									case Key::g7:
									case Key::g8:
									case Key::g9:
										break;
									case Key::play:
										data.push_back(0x9b);
										break;
									case Key::mute:
										data.push_back(0x9c);
										break;
									case Key::next:
										data.push_back(0x9d);
										break;
									case Key::prev:
										data.push_back(0x9e);
										break;
									case Key::ctrl_left:
									case Key::shift_left:
									case Key::alt_left:
									case Key::win_left:
									case Key::ctrl_right:
									case Key::shift_right:
									case Key::alt_right:
									case Key::win_right:
										data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) - 0x78);
										break;
									default:
										switch (static_cast<KeyAddressGroup>((static_cast<uint16_t>(x.second[gi+i].key) & 0xff00) / 0xff)) {
											case KeyAddressGroup::logo:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0xd1);
												break;
											case KeyAddressGroup::indicators:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0x98);
												break;
											case KeyAddressGroup::gkeys:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0xb3);
												break;
											case KeyAddressGroup::keys:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) - 0x03);
												break;
											default:
												break;
										}
								}
							}
						}
						
						if (data.size() < data_size) data.push_back(0xff);
						data.resize(data_size, 0x00);
						if (retval) retval = sendDataInternal(data);
						else sendDataInternal(data);
						
						gi = gi + maxKeyPerColor;
					}
				}
			}
			
			break;
		default:
			SortedKeys = {
				{}, // Logo AddressGroup
				{}, // Indicators AddressGroup
				{}, // Multimedia AddressGroup
				{}, // GKeys AddressGroup
				{} // Keys AddressGroup
			};
			
			for (uint8_t i = 0; i < keyValues.size(); i++) {
				switch(static_cast<LedKeyboard::KeyAddressGroup>(static_cast<uint16_t>(keyValues[i].key) >> 8 )) {
					case LedKeyboard::KeyAddressGroup::logo:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[0].size() <= 1 && keyValues[i].key == LedKeyboard::Key::logo)
									SortedKeys[0].push_back(keyValues[i]);
								break;
							case LedKeyboard::KeyboardModel::g910:
								if (SortedKeys[0].size() <= 2) SortedKeys[0].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::indicators:
						if (SortedKeys[1].size() <= 5) SortedKeys[1].push_back(keyValues[i]);
						break;
					case LedKeyboard::KeyAddressGroup::multimedia:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[2].size() <= 5) SortedKeys[2].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::gkeys:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g910:
								if (SortedKeys[3].size() <= 9) SortedKeys[3].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::keys:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g512:
							case LedKeyboard::KeyboardModel::g513:
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::g910:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[4].size() <= 120) SortedKeys[4].push_back(keyValues[i]);
								break;
							case LedKeyboard::KeyboardModel::g410:
								if (SortedKeys[4].size() <= 120)
									if (keyValues[i].key < LedKeyboard::Key::num_lock ||
										keyValues[i].key > LedKeyboard::Key::num_dot)
										SortedKeys[4].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
				}
			}
			
			for (uint8_t kag = 0; kag < 5; kag++) {
				
				if (SortedKeys[kag].size() > 0) {
					
					uint8_t gi = 0;
					while (gi < SortedKeys[kag].size()) {
						
						size_t data_size = 0;
						byte_buffer_t data = {};
						
						switch (kag) {
							case 0:
								data_size = 20;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::logo);
								break;
							case 1:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::indicators);
								break;
							case 2:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::multimedia);
								break;
							case 3:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::gkeys);
								break;
							case 4:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::keys);
								break;
						}
						
						const uint8_t maxKeyCount = (data_size - 8) / 4;
						
						if (data.size() > 0) {
							
							for (uint8_t i = 0; i < maxKeyCount; i++) {
								if (gi + i < SortedKeys[kag].size()) {
									data.push_back(static_cast<uint8_t>(
										static_cast<uint16_t>(SortedKeys[kag][gi+i].key) & 0x00ff));
									data.push_back(SortedKeys[kag][gi+i].color.red);
									data.push_back(SortedKeys[kag][gi+i].color.green);
									data.push_back(SortedKeys[kag][gi+i].color.blue);
								}
							}
							
							data.resize(data_size, 0x00);
							
							if (retval) retval = sendDataInternal(data);
							else sendDataInternal(data);
							
						}
						
						gi = gi + maxKeyCount;
					}
					
				}
			}
	}
	
	return retval;
}

bool LedKeyboard::setGroupKeys(KeyGroup keyGroup, LedKeyboard::Color color) {
	KeyValueArray keyValues;
	
	KeyArray keyArray;
	
	switch (keyGroup) {
		case KeyGroup::logo:
			keyArray = keyGroupLogo;
			break;
		case KeyGroup::indicators:
			keyArray = keyGroupIndicators;
			break;
		case KeyGroup::gkeys:
			keyArray = keyGroupGKeys;
			break;
		case KeyGroup::multimedia:
			keyArray = keyGroupMultimedia;
			break;
		case KeyGroup::fkeys:
			keyArray = keyGroupFKeys;
			break;
		case KeyGroup::modifiers:
			keyArray = keyGroupModifiers;
			break;
		case KeyGroup::arrows:
			keyArray = keyGroupArrows;
			break;
		case KeyGroup::numeric:
			keyArray = keyGroupNumeric;
			break;
		case KeyGroup::functions:
			keyArray = keyGroupFunctions;
			break;
		case KeyGroup::keys:
			keyArray = keyGroupKeys;
			break;
		default:
			break;
	}
	
	for (uint8_t i = 0; i < keyArray.size(); i++) keyValues.push_back({keyArray[i], color});
	
	return setKeys(keyValues);
}

bool LedKeyboard::setAllKeys(LedKeyboard::Color color) {
	KeyValueArray keyValues;

	switch (currentDevice.model) {
		case KeyboardModel::g213:
			for (uint8_t rIndex=0x01; rIndex <= 0x05; rIndex++) if (! setRegion(rIndex, color)) return false;
			return true;
		case KeyboardModel::g413:
			setNativeEffect(NativeEffect::color, NativeEffectPart::keys, std::chrono::seconds(0), color,
					NativeEffectStorage::none);
			return true;
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::g815:
		case KeyboardModel::g910:
		case KeyboardModel::gpro:
			for (uint8_t i = 0; i < keyGroupLogo.size(); i++) keyValues.push_back({keyGroupLogo[i], color});
			for (uint8_t i = 0; i < keyGroupIndicators.size(); i++) keyValues.push_back({keyGroupIndicators[i], color});
			for (uint8_t i = 0; i < keyGroupMultimedia.size(); i++) keyValues.push_back({keyGroupMultimedia[i], color});
			for (uint8_t i = 0; i < keyGroupGKeys.size(); i++) keyValues.push_back({keyGroupGKeys[i], color});
			for (uint8_t i = 0; i < keyGroupFKeys.size(); i++) keyValues.push_back({keyGroupFKeys[i], color});
			for (uint8_t i = 0; i < keyGroupFunctions.size(); i++) keyValues.push_back({keyGroupFunctions[i], color});
			for (uint8_t i = 0; i < keyGroupArrows.size(); i++) keyValues.push_back({keyGroupArrows[i], color});
			for (uint8_t i = 0; i < keyGroupNumeric.size(); i++) keyValues.push_back({keyGroupNumeric[i], color});
			for (uint8_t i = 0; i < keyGroupModifiers.size(); i++) keyValues.push_back({keyGroupModifiers[i], color});
			for (uint8_t i = 0; i < keyGroupKeys.size(); i++) keyValues.push_back({keyGroupKeys[i], color});
			return setKeys(keyValues);
		default:
			return false;
	}
	return false;
}


bool LedKeyboard::setMRKey(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x13;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x0c;
			}
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, g815_target, g815_feat_idx, 0x0c, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x0a, 0x0e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setMNKey(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x12;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x0b;
			}
			switch (value) {
				case 0x01:
                    data = { 0x11, g815_target, g815_feat_idx, 0x1c, 0x01 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
                case 0x02:
                    data = { 0x11, g815_target, g815_feat_idx, 0x1c, 0x02 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
                case 0x03:
                    data = { 0x11, g815_target, g815_feat_idx, 0x1c, 0x04 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
					data = { 0x11, 0xff, 0x09, 0x1e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setGKeysMode(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x11;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x0a;
			}
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, g815_target, g815_feat_idx, 0x2b, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x08, 0x2e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setRegion(uint8_t region, LedKeyboard::Color color) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
			data = { 0x11, 0xff, 0x0c, 0x3a, region, 0x01, color.red, color.green, color.blue };
			data.resize(20,0x00);
			return sendDataInternal(data);
			break;
		default:
			break;
	}

	return false;
}

bool LedKeyboard::setStartupMode(StartupMode startupMode) {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g410:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			data = { 0x11, 0xff, 0x0d, 0x5a, 0x00, 0x01 };
			break;
		case KeyboardModel::g910:
			data = { 0x11, 0xff, 0x10, 0x5e, 0x00, 0x01 };
			break;
		default:
			return false;
	}
	data.push_back((unsigned char)startupMode);
	data.resize(20, 0x00);
	return sendDataInternal(data);
}

bool LedKeyboard::setOnBoardMode(OnBoardMode onBoardMode) {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x15;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x11;
			}
			data = { 0x11, g815_target, g815_feat_idx, 0x1a, static_cast<uint8_t>(onBoardMode) };
			data.resize(20, 0x00);
			return sendDataInternal(data);
		default:
			return false;
	}
}

bool LedKeyboard::setNativeEffect(NativeEffect effect, NativeEffectPart part,
				  std::chrono::duration<uint16_t, std::milli> period, Color color,
				  NativeEffectStorage storage) {
	uint8_t protocolBytes[2] = {0x00, 0x00};
	NativeEffectGroup effectGroup = static_cast<NativeEffectGroup>(static_cast<uint16_t>(effect) >> 8);

	// NativeEffectPart::all is not in the device protocol, but an alias for both keys and logo, plus indicators
	if (part == LedKeyboard::NativeEffectPart::all) {
		switch (effectGroup) {
			case NativeEffectGroup::color:
				if (! setGroupKeys(LedKeyboard::KeyGroup::indicators, color)) return false;
				if (! commit()) return false;
				break;
			case NativeEffectGroup::breathing:
				if (! setGroupKeys(LedKeyboard::KeyGroup::indicators, color)) return false;;
				if (! commit()) return false;;
				break;
			case NativeEffectGroup::cycle:
			case NativeEffectGroup::waves:
			case NativeEffectGroup::ripple:
				if (! setGroupKeys(
					LedKeyboard::KeyGroup::indicators,
					LedKeyboard::Color({0xff, 0xff, 0xff}))
				) return false;
				if (! commit()) return false;
				break;
			default:
				break;
		}
		return (
			setNativeEffect(effect, LedKeyboard::NativeEffectPart::keys, period, color, storage) &&
			setNativeEffect(effect, LedKeyboard::NativeEffectPart::logo, period, color, storage));
	}

	unsigned char target = 0xff;

	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
			protocolBytes[0] = 0x0c;
			protocolBytes[1] = 0x3c;
			if (part == NativeEffectPart::logo) return true; //Does not have logo component
			break;
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610: // Unconfirmed
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			protocolBytes[0] = 0x0d;
			protocolBytes[1] = 0x3c;
			break;
		case KeyboardModel::g815:
			protocolBytes[0] = 0x0f;
			protocolBytes[1] = 0x1c;
			break;
		case KeyboardModel::g915:
			protocolBytes[0] = 0x0a;
			protocolBytes[1] = 0x1c;
			target = 0x01;
			break;
		case KeyboardModel::g910:
			protocolBytes[0] = 0x10;
			protocolBytes[1] = 0x3c;
			break;
		default:
			return false;
	}

	byte_buffer_t data = {
		0x11, target, protocolBytes[0], protocolBytes[1],
		(uint8_t)part, static_cast<uint8_t>(effectGroup),
		// color of static-color and breathing effects
		color.red, color.green, color.blue,
		// period of breathing effect (ms)
		static_cast<uint8_t>(period.count() >> 8), static_cast<uint8_t>(period.count() & 0xff),
		// period of cycle effect (ms)
		static_cast<uint8_t>(period.count() >> 8), static_cast<uint8_t>(period.count() & 0xff),
		static_cast<uint8_t>(static_cast<uint16_t>(effect) & 0xff), // wave variation (e.g. horizontal)
		0x64, // unused?
		// period of wave effect (ms)
		static_cast<uint8_t>(period.count() >> 8), // LSB is shared with cycle effect above
		static_cast<uint8_t>(storage),
		0, // unused?
		0, // unused?
		0, // unused?
	};

	byte_buffer_t setupData;
	bool retval;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
		case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x0a;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x0f;
			}
			setupData = { 0x11, g815_target, g815_feat_idx, 0x5c, 0x01, 0x03, 0x03 };
			setupData.resize(20, 0x00);
			retval = sendDataInternal(setupData);

			data[16] = 0x01;

			switch (part) {
				case NativeEffectPart::keys:
					data[4] = 0x01;

					//Seems to conflict with a star-like effect on G410 and G810
					switch (effect) {
						case NativeEffect::ripple:
							//Adjust periodicity
							data[9]=0x00;
							data[10]=period.count() >> 8 & 0xff;;
							data[11]=period.count() & 0xff;
							data[12]=0x00;
							break;
						default:
							break;
					}
					break;
				case NativeEffectPart::logo:
					data[4] = 0x00;
					switch (effect) {
						case NativeEffect::breathing:
							data[5]=0x03;
							break;
						case NativeEffect::cwave:
						case NativeEffect::vwave:
						case NativeEffect::hwave:
							data[5]=0x02;
							data[13]=0x64;
							break;
						case NativeEffect::waves:
						case NativeEffect::cycle:
							data[5]=0x02;
							break;
						case NativeEffect::ripple:
						case NativeEffect::off:
							data[5]=0x00;
							break;
						default:
							data[5]=0x01;
							break;
					}
					break;
				default:
					break;
			}
			break;
		default: //Many devices may not support logo coloring for wave?
			if ((effectGroup == NativeEffectGroup::waves) && (part == NativeEffectPart::logo)) {
				return setNativeEffect(NativeEffect::color, part, std::chrono::seconds(0), Color({0x00, 0xff, 0xff}), storage);
			}
			break;
	}
	retval = sendDataInternal(data);
	return retval;
}


bool LedKeyboard::sendDataInternal(byte_buffer_t &data) {
	if (data.size() > 0) {
		#if defined(hidapi)
			if (! open(currentDevice.vendorID, currentDevice.productID, currentDevice.serialNumber)) return false;
			if (hid_write(m_hidHandle, const_cast<unsigned char*>(data.data()), data.size()) < 0) {
				std::cout<<"Error: Can not write to hidraw, try with the libusb version"<<std::endl;
				return false;
			}
			close();
			/*
			byte_buffer_t data2;
			data2.resize(21, 0x00);
			hid_read_timeout(m_hidHandle, const_cast<unsigned char*>(data2.data()), data2.size(), 1);
			*/
			return true;
		#elif defined(libusb)
			int interface_num;
			int interrupt_endpoint;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					interface_num = 2;
					interrupt_endpoint = 0x83;
					break;
				default:
					interface_num = 1;
					interrupt_endpoint = 0x82;
			}

			if (! m_isOpen) return false;
			if (data.size() > 20) {
				if(libusb_control_transfer(m_hidHandle, 0x21, 0x09, 0x0212, interface_num, 
						const_cast<unsigned char*>(data.data()), data.size(), 2000) < 0)
					return false;
			} else {
				if(libusb_control_transfer(m_hidHandle, 0x21, 0x09, 0x0211, interface_num, 
						const_cast<unsigned char*>(data.data()), data.size(), 2000) < 0)
					return false;
			}
			usleep(1000);
			unsigned char buffer[64];
			int len = 0;
			libusb_interrupt_transfer(m_hidHandle, interrupt_endpoint, buffer, sizeof(buffer), &len, 1);
			return true;
		#endif
	}
	
	return false;
}

LedKeyboard::byte_buffer_t LedKeyboard::getKeyGroupAddress(LedKeyboard::KeyAddressGroup keyAddressGroup) {
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
		  return {}; // Device doesn't support per-key setting
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::gpro:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0c, 0x3a, 0x00, 0x10, 0x00, 0x01 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return {};
				case LedKeyboard::KeyAddressGroup::multimedia:
					return {};
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		case KeyboardModel::g610:
		case KeyboardModel::g810:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0c, 0x3a, 0x00, 0x10, 0x00, 0x01 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return {};
				case LedKeyboard::KeyAddressGroup::multimedia:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x02, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		case KeyboardModel::g815:
			case KeyboardModel::g915:
			unsigned char g815_target;
			unsigned char g815_feat_idx;
			switch (currentDevice.model) {
				case KeyboardModel::g915:
					g815_target = 0x01;
					g815_feat_idx = 0x0b;
					break;
				default:
					g815_target = 0xff;
					g815_feat_idx = 0x10;
			}
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, g815_target, g815_feat_idx, 0x1c };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x11, g815_target, g815_feat_idx, 0x1c };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return { 0x11, g815_target, g815_feat_idx, 0x1c };
				case LedKeyboard::KeyAddressGroup::multimedia:
					return { 0x11, g815_target, g815_feat_idx, 0x1c };
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x11, g815_target, g815_feat_idx, 0x1c };
			}
			break;
		case KeyboardModel::g910:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0f, 0x3a, 0x00, 0x10, 0x00, 0x02 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return { 0x12, 0xff, 0x0f, 0x3e, 0x00, 0x04, 0x00, 0x09 };
				case LedKeyboard::KeyAddressGroup::multimedia:
					return {};
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0f, 0x3d, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		default:
			break;
	}
	return {};
}
