#include "stdafx.h"
#include <iostream>
#include <sstream>
#include <cassert>
#include <memory>
#include <algorithm>
#include <Windows.h>
#include "UserControl.h"
#include "BS_Errno.h"
#include "../Common/Utility.h"
#include "../Common/ConfigControl.h"


extern void TRACE(const char* fmt, ...);
using namespace std;

void* UserControl::context_ = NULL;

UserControl::UserControl(void* sdkContext)
{
	context_ = sdkContext;
}


UserControl::~UserControl()
{
}

int UserControl::isAcceptableUID(const char* uid)
{
	const size_t idLen = strlen(uid);
	const string idMax = MAX_SUPPORTED_UID;
	if (idMax.size() < idLen)
		return 0;

	if (idMax.size() == idLen && idMax.compare(uid) < 0)
		return 0;

	return 1;
}

void UserControl::onReadyToScan(BS2_DEVICE_ID id, uint32_t sequence)
{
	cout << "Place your card(/finger/face) on the device. [" << id << ", " << "Seq:" << sequence << "]" << endl;
}

int UserControl::getUser(BS2_DEVICE_ID id)
{
	string uid;
	uid.resize(BS2_USER_ID_SIZE);
	uid = Utility::getInput<string>("Please enter a user ID :");
	if (BS2_USER_ID_SIZE < uid.size())
	{
		TRACE("User ID is too big.");
		return BS_SDK_ERROR_INVALID_PARAM;
	}

	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };
	stringstream msg;
	msg << "What do you want :\n";
	msg << "[1: User header, 2: Card, 3: Finger, 4: Face]";
	short maskType = Utility::getInput<short>(msg.str());

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}
	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;

	uint32_t numUser = 1;
	BS2_USER_MASK userMask = 0;
	size_t dataSize = 0;
	//shared_ptr<uint8_t> ptrUID(new uint8_t[BS2_USER_ID_SIZE], ArrayDeleter<uint8_t>());

	userMask = BS2_USER_MASK_DATA | BS2_USER_MASK_SETTING;
	switch (maskType)
	{
	case BS_USER_HEADER_ONLY:
		userMask |= BS2_USER_MASK_ACCESS_GROUP;
		if (deviceInfo.userNameSupported)
			userMask |= BS2_USER_MASK_NAME;
		if (deviceInfo.pinSupported)
			userMask |= BS2_USER_MASK_PIN;
		if (deviceInfo.userPhotoSupported)
			userMask |= BS2_USER_MASK_PHOTO;
		break;
	case BS_USER_CRED_CARD:
		if (deviceInfo.cardSupported)
			userMask |= BS2_USER_MASK_CARD;
		break;
	case BS_USER_CRED_FINGER:
		if (fingerScanSupported)
			userMask |= BS2_USER_MASK_FINGER;
		break;
	case BS_USER_CRED_FACE:
		if (faceScanSupported)
			userMask |= BS2_USER_MASK_FACE;
		break;
	default:
		return BS_SDK_ERROR_INVALID_PARAM;
	}

	BS2UserBlob userBlob = { 0, };
	sdkResult = BS2_GetUserDatas(context_, id, const_cast<char*>(uid.c_str()), numUser, &userBlob, userMask);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetUserDatas call failed: %d", sdkResult);
		return sdkResult;
	}

	switch (maskType)
	{
	case BS_USER_HEADER_ONLY:
		printHeader(userBlob);
		break;
	case BS_USER_CRED_CARD:
		print(userBlob.cardObjs, userBlob.user.numCards);
		if (0 < userBlob.user.numCards)
			BS2_ReleaseObject(userBlob.cardObjs);
		break;
	case BS_USER_CRED_FINGER:
		print(userBlob.fingerObjs, userBlob.user.numFingers);
		if (0 < userBlob.user.numFingers)
			BS2_ReleaseObject(userBlob.fingerObjs);
		break;
	case BS_USER_CRED_FACE:
		print(userBlob.faceObjs, userBlob.user.numFaces);
		if (0 < userBlob.user.numFaces)
			BS2_ReleaseObject(userBlob.faceObjs);
		break;
	}

	return sdkResult;
}

int UserControl::getAllUser(BS2_DEVICE_ID id)
{
	char* uidObj = NULL;
	uint32_t numUID = 0;

	int sdkResult = BS2_GetUserList(context_, id, &uidObj, &numUID, isAcceptableUID);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetUserList call failed: %d", sdkResult);
		return sdkResult;
	}

	const int MAX_USER_PAGE_COUNT = 2048;
	vector<BS2UserBlob> userBlob(MAX_USER_PAGE_COUNT);
	BS2_USER_MASK userMask = BS2_USER_MASK_ALL;

	for (uint32_t idx = 0; idx < numUID; idx += MAX_USER_PAGE_COUNT)
	{
		uint32_t availUserCount = min<uint32_t>(MAX_USER_PAGE_COUNT, numUID - idx);

		int sdkResult = BS2_GetUserDatas(context_, id, uidObj + BS2_USER_ID_SIZE * idx, availUserCount, &userBlob[0], userMask);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_GetUserDatas call failed: %d", sdkResult);
			if (uidObj)
				BS2_ReleaseObject(uidObj);
			return sdkResult;
		}

		for (uint32_t pageIdx = 0; pageIdx < availUserCount; pageIdx++)
		{
			print(userBlob[pageIdx]);

			if (0 < userBlob[pageIdx].user.numCards && userBlob[pageIdx].cardObjs)
				BS2_ReleaseObject(userBlob[pageIdx].cardObjs);
			if (0 < userBlob[pageIdx].user.numFingers && userBlob[pageIdx].fingerObjs)
				BS2_ReleaseObject(userBlob[pageIdx].fingerObjs);
			if (0 < userBlob[pageIdx].user.numFaces && userBlob[pageIdx].faceObjs)
				BS2_ReleaseObject(userBlob[pageIdx].faceObjs);
		}
	}

	if (uidObj)
		BS2_ReleaseObject(uidObj);

	return sdkResult;
}

int UserControl::getAllUserFaceEx(BS2_DEVICE_ID id)
{
	char* uidObj = NULL;
	uint32_t numUID = 0;

	int sdkResult = BS2_GetUserList(context_, id, &uidObj, &numUID, isAcceptableUID);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetUserList call failed: %d", sdkResult);
		return sdkResult;
	}

	const int MAX_USER_PAGE_COUNT = 2048;
	vector<BS2UserFaceExBlob> userBlob(MAX_USER_PAGE_COUNT);
	BS2_USER_MASK userMask = BS2_USER_MASK_ALL;

	for (uint32_t idx = 0; idx < numUID; idx += MAX_USER_PAGE_COUNT)
	{
		uint32_t availUserCount = min<uint32_t>(MAX_USER_PAGE_COUNT, numUID - idx);

		int sdkResult = BS2_GetUserDatasFaceEx(context_, id, uidObj + BS2_USER_ID_SIZE * idx, availUserCount, &userBlob[0], userMask);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_GetUserDatas call failed: %d", sdkResult);
			if (uidObj)
				BS2_ReleaseObject(uidObj);
			return sdkResult;
		}

		for (uint32_t pageIdx = 0; pageIdx < availUserCount; pageIdx++)
		{
			print(userBlob[pageIdx]);

			if (0 < userBlob[pageIdx].user.numCards && userBlob[pageIdx].cardObjs)
				BS2_ReleaseObject(userBlob[pageIdx].cardObjs);
			if (0 < userBlob[pageIdx].user.numFingers && userBlob[pageIdx].fingerObjs)
				BS2_ReleaseObject(userBlob[pageIdx].fingerObjs);
			if (0 < userBlob[pageIdx].user.numFaces && userBlob[pageIdx].faceExObjs)
				BS2_ReleaseObject(userBlob[pageIdx].faceExObjs);
		}
	}

	if (uidObj)
		BS2_ReleaseObject(uidObj);

	return sdkResult;
}

int UserControl::enrollUser(BS2_DEVICE_ID id)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;

	BS2UserBlob userBlob = { 0, };
	BS2User& user = userBlob.user;
	BS2UserSetting& setting = userBlob.setting;
	BS2UserPhoto& photo = userBlob.user_photo;
	stringstream msg;

	string uid = Utility::getInput<string>("Please enter a user ID:");
	if (BS2_USER_ID_SIZE < uid.size())
	{
		TRACE("User ID is too big.");
		return BS_SDK_ERROR_INVALID_PARAM;
	}
	strcpy(user.userID, uid.c_str());

	if (deviceInfo.userNameSupported)
	{
		string name = Utility::getInput<string>("Enter your name:");
		if (BS2_USER_NAME_SIZE < name.size())
		{
			TRACE("User name is too long.");
			return BS_SDK_ERROR_INVALID_PARAM;
		}
		strcpy(reinterpret_cast<char*>(userBlob.user_name), name.c_str());
	}

	{
		string inputTime = Utility::getLine("Please enter start time [YYYY-MM-DD HH:MM:SS] ?");
		BS2_TIMESTAMP startTime = Utility::convertTimeString2UTC(inputTime);
		setting.startTime = startTime;

		inputTime = Utility::getLine("Please enter end time [YYYY-MM-DD HH:MM:SS] ?");
		BS2_TIMESTAMP endTime = Utility::convertTimeString2UTC(inputTime);
		setting.endTime = endTime;
	}

	if (deviceInfo.pinSupported)
	{
		string pinString = Utility::getInput<string>("Enter the PIN code:");
		if (BS2_USER_PIN_SIZE < pinString.size())
		{
			TRACE("PIN code is too long");
			return BS_SDK_ERROR_INVALID_PARAM;
		}

		sdkResult = BS2_MakePinCode(context_, const_cast<char*>(pinString.c_str()), userBlob.pin);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_MakePinCode call failed: %d", sdkResult);
			return sdkResult;
		}
	}

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;

	if (fingerScanSupported || faceScanSupported)
	{
		msg.str("");
		msg << "Enter the biometric authentication mode\n";
		msg << "[1: Biometric only, 2: Biometric+PIN]";
		int fingerAuthMode = Utility::getInput<int>(msg.str());
		switch (fingerAuthMode)
		{
		case 1:
			setting.fingerAuthMode = BS2_AUTH_MODE_BIOMETRIC_ONLY;
			break;
		case 2:
			setting.fingerAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_BIOMETRIC_PIN : BS2_AUTH_MODE_BIOMETRIC_ONLY;
			break;
		default:
			setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
			break;
		}
	}

	setting.cardAuthMode = BS2_AUTH_MODE_NONE;

	if (deviceInfo.cardSupported)
	{
		msg.str("");
		msg << "Enter the card authentication mode\n";
		msg << "[1: Card only, 2: Card+Biometric, 3: Card+PIN, 4: Card+(Biometric/PIN), 5: Card+Biometric+PIN]";
		int cardAuthMode = Utility::getInput<int>(msg.str());
		switch (cardAuthMode)
		{
		case 1:
			setting.cardAuthMode = BS2_AUTH_MODE_CARD_ONLY;
			break;
		case 2:
			setting.cardAuthMode = (fingerScanSupported || faceScanSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC : BS2_AUTH_MODE_CARD_ONLY;
			break;
		case 3:
			setting.cardAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_CARD_PIN : BS2_AUTH_MODE_CARD_ONLY;
			break;
		case 4:
			setting.cardAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC_OR_PIN : BS2_AUTH_MODE_CARD_ONLY;
			break;
		case 5:
			setting.cardAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC_PIN : BS2_AUTH_MODE_CARD_ONLY;
			break;
		default:
			setting.cardAuthMode = BS2_AUTH_MODE_NONE;
			break;
		}
	}

	setting.idAuthMode = BS2_AUTH_MODE_NONE;

	{
		msg.str("");
		msg << "Enter the ID authentication mode\n";
		msg << "[1: ID+Biometric, 2: ID+PIN, 3: ID+(Biometric/PIN), 4: ID+Biometric+PIN]";
		int idAuthMode = Utility::getInput<int>(msg.str());
		switch (idAuthMode)
		{
		case 1:
			setting.idAuthMode = (fingerScanSupported || faceScanSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC : BS2_AUTH_MODE_NONE;
			break;
		case 2:
			setting.idAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_ID_PIN : BS2_AUTH_MODE_NONE;
			break;
		case 3:
			setting.idAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC_OR_PIN : BS2_AUTH_MODE_NONE;
			break;
		case 4:
			setting.idAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC_PIN : BS2_AUTH_MODE_NONE;
			break;
		default:
			setting.idAuthMode = BS2_AUTH_MODE_NONE;
			break;
		}
	}

	{
		msg.str("");
		msg << "Enter the security level for this user\n";
		msg << "[0: Default, 1: Lower, 2: Low, 3: Normal, 4: High, 5, Higher]";
		int securityLevel = Utility::getInput<int>(msg.str());
		switch (securityLevel)
		{
		case BS2_USER_SECURITY_LEVEL_DEFAULT:
		case BS2_USER_SECURITY_LEVEL_LOWER:
		case BS2_USER_SECURITY_LEVEL_LOW:
		case BS2_USER_SECURITY_LEVEL_NORMAL:
		case BS2_USER_SECURITY_LEVEL_HIGH:
		case BS2_USER_SECURITY_LEVEL_HIGHER:
			setting.securityLevel = securityLevel;
			break;
		default:
			setting.securityLevel = BS2_USER_SECURITY_LEVEL_DEFAULT;
			break;
		}
	}

	if (deviceInfo.userPhotoSupported)
	{
		char profileImage = Utility::getInput<char>("Do you want to register a profile image? [y/n]");
		if ('y' == profileImage || 'Y' == profileImage)
		{
			string imagePath = Utility::getInput<string>("Enter the profile image path and name:");
			uint32_t size = Utility::getResourceSize(imagePath);
			shared_ptr<uint8_t> buffer(new uint8_t[size], ArrayDeleter<uint8_t>());

			while (BS2_USER_PHOTO_SIZE < size)
			{
				msg.str("");
				msg << "Image is to big.\n";
				msg << "Re-enter an image smaller than 16384 byte:";
				imagePath = Utility::getInput<string>(msg.str());
				size = Utility::getResourceSize(imagePath);
			}

			if (Utility::getResourceFromFile(imagePath, buffer, size))
			{
				photo.size = size;
				memcpy(photo.data, buffer.get(), size);
			}
		}
	}

	char flag = Utility::getInput<char>("Do you want to register access group ID? [y/n]");
	if ('y' == flag || 'Y' == flag)
	{
		msg.str("");
		msg << "Please enter access group IDs. ex)ID1 ID2 ID3 ...\n";
		string inStrAGID = Utility::getLine(msg.str());
		if (0 == inStrAGID.size())
			return BS_SDK_ERROR_CANNOT_FIND_ACCESS_GROUP;

		vector<string> listID = Utility::tokenizeString(inStrAGID);
		if (listID.size() < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER)
		{
			uint32_t index(0);
			for (auto groupID : listID)
			{
				BS2_ACCESS_GROUP_ID gid = atoi(groupID.c_str());
				userBlob.accessGroupId[index++] = gid;
			}
		}
	}

	{
		msg.str("");
		msg << "Please enter a authentication group ID.\n";
		msg << "This is used for face authentication. [0: Not using]";
		uint32_t authGroupID = Utility::getInput<uint32_t>(msg.str());
		user.authGroupID = authGroupID;
	}

	{
		flag = Utility::getInput<char>("Do you want to overwrite the user if it exist? [y/n]");
		user.flag = (flag == 'y' || flag == 'Y') ? BS2_USER_FLAG_CREATED | BS2_USER_FLAG_UPDATED : BS2_USER_FLAG_CREATED;
	}

	user.numFingers = 0;
	user.numCards = 0;
	user.numFaces = 0;

	if (deviceInfo.cardSupported)
	{
		flag = Utility::getInput<char>("Do you want to scan card? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numCard = Utility::getInput<uint32_t>("How many cards would you like to register?");
			BS2CSNCard* ptrCard = new BS2CSNCard[numCard];
			if (ptrCard)
			{
				userBlob.cardObjs = ptrCard;
				for (uint32_t index = 0; index < numCard;)
				{
					BS2Card card = { 0, };
					sdkResult = BS2_ScanCard(context_, id, &card, onReadyToScan);
					if (BS_SDK_SUCCESS != sdkResult)
						TRACE("BS2_ScanCard call failed: %d", sdkResult);
					else
					{
						if (card.isSmartCard)
						{
							TRACE("CSN card only supported.");
						}
						else
						{
							memcpy(&ptrCard[index], &card.card, sizeof(BS2CSNCard));
						}
						user.numCards++;
						index++;
					}
				}
			}
		}
	}

	if (fingerScanSupported)
	{
		flag = Utility::getInput<char>("Do you want to scan fingerprint? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFinger = Utility::getInput<uint32_t>("How many fingers would you like to register?");
			BS2Fingerprint* ptrFinger = new BS2Fingerprint[numFinger];
			if (ptrFinger)
			{
				userBlob.fingerObjs = ptrFinger;
				for (uint32_t index = 0; index < numFinger; index++)
				{
					for (uint32_t templateIndex = 0; templateIndex < BS2_TEMPLATE_PER_FINGER;)
					{
						sdkResult = BS2_ScanFingerprint(context_, id, &ptrFinger[index], templateIndex, BS2_FINGER_TEMPLATE_QUALITY_HIGHEST, BS2_FINGER_TEMPLATE_FORMAT_SUPREMA, onReadyToScan);
						if (BS_SDK_SUCCESS != sdkResult)
							TRACE("BS2_ScanFingerprint call failed: %d", sdkResult);
						else
							templateIndex++;
					}
					user.numFingers++;
				}
			}
		}
	}

	if (faceScanSupported)
	{
		flag = Utility::getInput<char>("Do you want to scan face? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFace = Utility::getInput<uint32_t>("How many face would you like to register?");
			BS2Face* ptrFace = new BS2Face[numFace];
			if (ptrFace)
			{
				userBlob.faceObjs = ptrFace;
				for (uint32_t index = 0; index < numFace;)
				{
					sdkResult = BS2_ScanFace(context_, id, &ptrFace[index], BS2_FACE_ENROLL_THRESHOLD_DEFAULT, onReadyToScan);
					if (BS_SDK_SUCCESS != sdkResult)
						TRACE("BS2_ScanFace call failed: %d", sdkResult);
					else
					{
						user.numFaces++;
						index++;
					}
				}
			}
		}
	}

	sdkResult = BS2_EnrolUser(context_, id, &userBlob, 1, 1);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_EnrolUser call failed: %d", sdkResult);

	if (userBlob.cardObjs)
		delete[] userBlob.cardObjs;

	if (userBlob.fingerObjs)
		delete[] userBlob.fingerObjs;

	if (userBlob.faceObjs)
		delete[] userBlob.faceObjs;

	return sdkResult;
}

int UserControl::enrollUserSmall(BS2_DEVICE_ID id)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;

	BS2UserSmallBlob userBlob = { 0, };
	BS2User& user = userBlob.user;
	BS2UserSetting& setting = userBlob.setting;
	BS2UserPhoto& photo = *userBlob.user_photo_obj;
	stringstream msg;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserName(userBlob, deviceInfo)))
		return sdkResult;

	getUserBlobExpiryDate(setting);

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPINCode(userBlob, deviceInfo)))
		return sdkResult;

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
	setting.cardAuthMode = BS2_AUTH_MODE_NONE;
	setting.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthMode(setting, deviceInfo, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobSecurityLevel(setting)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobProfileImage(userBlob, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobAccessGroupID(userBlob)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceAuthGroupID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserUpdate(user)))
		return sdkResult;

	user.numFingers = 0;
	user.numCards = 0;
	user.numFaces = 0;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobCardInfo(userBlob, id, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFingerprintInfo(userBlob, id, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceInfo(userBlob, id, deviceInfoEx)))
		return sdkResult;

	sdkResult = BS2_EnrollUserSmall(context_, id, &userBlob, 1, 1);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_EnrollUserSmall call failed: %d", sdkResult);

	if (userBlob.cardObjs)
		delete[] userBlob.cardObjs;

	if (userBlob.fingerObjs)
		delete[] userBlob.fingerObjs;

	if (userBlob.faceObjs)
		delete[] userBlob.faceObjs;

	return sdkResult;
}

int UserControl::getUserFaceEx(BS2_DEVICE_ID id)
{
	string uid;
	uid.resize(BS2_USER_ID_SIZE);
	uid = Utility::getInput<string>("Please enter a user ID :");
	if (BS2_USER_ID_SIZE < uid.size())
	{
		TRACE("User ID is too big.");
		return BS_SDK_ERROR_INVALID_PARAM;
	}

	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };
	stringstream msg;
	msg << "What do you want :\n";
	msg << "[1: User header, 2: Card, 3: Finger, 4: FaceEx]";
	short maskType = Utility::getInput<short>(msg.str());

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	uint32_t numUser = 1;
	BS2_USER_MASK userMask = 0;
	size_t dataSize = 0;
	//shared_ptr<uint8_t> ptrUID(new uint8_t[BS2_USER_ID_SIZE], ArrayDeleter<uint8_t>());

	userMask = BS2_USER_MASK_DATA | BS2_USER_MASK_SETTING;
	switch (maskType)
	{
	case BS_USER_HEADER_ONLY:
		userMask |= BS2_USER_MASK_ACCESS_GROUP;
		if (deviceInfo.userNameSupported)
			userMask |= BS2_USER_MASK_NAME;
		if (deviceInfo.pinSupported)
			userMask |= BS2_USER_MASK_PIN;
		if (deviceInfo.userPhotoSupported)
			userMask |= BS2_USER_MASK_PHOTO;
		break;
	case BS_USER_CRED_CARD:
		if (deviceInfo.cardSupported)
			userMask |= BS2_USER_MASK_CARD;
		break;
	case BS_USER_CRED_FINGER:
		if (fingerScanSupported)
			userMask |= BS2_USER_MASK_FINGER;
		break;
	case BS_USER_CRED_FACE:
		if (faceExScanSupported)
			userMask |= (BS2_USER_MASK_SETTING_EX | BS2_USER_MASK_FACE_EX);
		break;
	default:
		return BS_SDK_ERROR_INVALID_PARAM;
	}

	BS2UserFaceExBlob userBlob = { 0, };
	sdkResult = BS2_GetUserDatasFaceEx(context_, id, const_cast<char*>(uid.c_str()), numUser, &userBlob, userMask);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetUserDatasFaceEx call failed: %d", sdkResult);
		return sdkResult;
	}

	switch (maskType)
	{
	case BS_USER_HEADER_ONLY:
		printHeaderFaceEx(userBlob);
		break;
	case BS_USER_CRED_CARD:
		if (userBlob.cardObjs && 0 < userBlob.user.numCards)
		{
			print(userBlob.cardObjs, userBlob.user.numCards);
			BS2_ReleaseObject(userBlob.cardObjs);
		}
		break;
	case BS_USER_CRED_FINGER:
		if (userBlob.fingerObjs && 0 < userBlob.user.numFingers)
		{
			print(userBlob.fingerObjs, userBlob.user.numFingers);
			BS2_ReleaseObject(userBlob.fingerObjs);
		}
		break;
	case BS_USER_CRED_FACE:
		if (userBlob.faceExObjs && 0 < userBlob.user.numFaces)
		{
			print(userBlob.faceExObjs, userBlob.user.numFaces);
			BS2_ReleaseObject(userBlob.faceExObjs);
		}
		break;
	}

	return sdkResult;
}

int UserControl::makeUserFaceEx(BS2_DEVICE_ID id, BS2UserFaceExBlob* userBlob)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	BS2User& user = userBlob->user;
	BS2UserSetting& setting = userBlob->setting;
	BS2UserSettingEx& settingEx = userBlob->settingEx;
	userBlob->user_photo_obj = NULL;
	stringstream msg;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserName(*userBlob, deviceInfo)))
		return sdkResult;

	getUserBlobExpiryDate(setting);

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPINCode(*userBlob, deviceInfo)))
		return sdkResult;

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
	setting.cardAuthMode = BS2_AUTH_MODE_NONE;
	setting.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthMode(setting, deviceInfo, deviceInfoEx)))
		return sdkResult;

	settingEx.faceAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.fingerprintAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.cardAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthModeEx(settingEx, deviceInfo, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobSecurityLevel(setting)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobProfileImage(*userBlob, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobAccessGroupID(*userBlob)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceAuthGroupID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserUpdate(user)))
		return sdkResult;

	user.numFingers = 0;
	user.numCards = 0;
	user.numFaces = 0;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobCardInfo(*userBlob, id, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFingerprintInfo(*userBlob, id, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceInfo(*userBlob, id, deviceInfoEx)))
		return sdkResult;

	return sdkResult;
}

int UserControl::makeUserFaceExWithImage(BS2_DEVICE_ID id, const BS2TemplateEx& templateEx, BS2UserFaceExBlob* userBlob)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	BS2User& user = userBlob->user;
	BS2UserSetting& setting = userBlob->setting;
	BS2UserSettingEx& settingEx = userBlob->settingEx;
	userBlob->user_photo_obj = NULL;
	stringstream msg;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserName(*userBlob, deviceInfo)))
		return sdkResult;

	getUserBlobExpiryDate(setting);

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPINCode(*userBlob, deviceInfo)))
		return sdkResult;

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
	setting.cardAuthMode = BS2_AUTH_MODE_NONE;
	setting.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthMode(setting, deviceInfo, deviceInfoEx)))
		return sdkResult;

	settingEx.faceAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.fingerprintAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.cardAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthModeEx(settingEx, deviceInfo, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobSecurityLevel(setting)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobProfileImage(*userBlob, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobAccessGroupID(*userBlob)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceAuthGroupID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserUpdate(user)))
		return sdkResult;

	user.numFingers = 0;
	user.numCards = 0;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobCardInfo(*userBlob, id, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFingerprintInfo(*userBlob, id, deviceInfoEx)))
		return sdkResult;

	user.numFaces = 1;
	userBlob->faceExObjs = new BS2FaceEx[user.numFaces];
	memset(userBlob->faceExObjs, 0x0, sizeof(BS2FaceEx) * user.numFaces);

	BS2FaceEx& faceEx = userBlob->faceExObjs[0];

	faceEx.numOfTemplate = 1;
	faceEx.flag = 1;
	memcpy(faceEx.templateEx, &templateEx, sizeof(BS2TemplateEx));

	return sdkResult;
}

int UserControl::enrollUserFaceEx(BS2_DEVICE_ID id, BS2CSNCard* card, BS2Fingerprint* finger, BS2Face* face, BS2FaceEx* faceEx)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	BS2UserFaceExBlob userBlob = { 0, };
	BS2User& user = userBlob.user;
	BS2UserSetting& setting = userBlob.setting;
	BS2UserSettingEx& settingEx = userBlob.settingEx;
	userBlob.user_photo_obj = NULL;
	stringstream msg;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserName(userBlob, deviceInfo)))
		return sdkResult;

	getUserBlobExpiryDate(setting);

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPINCode(userBlob, deviceInfo)))
		return sdkResult;

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
	setting.cardAuthMode = BS2_AUTH_MODE_NONE;
	setting.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthMode(setting, deviceInfo, deviceInfoEx)))
		return sdkResult;

	settingEx.faceAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.fingerprintAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.cardAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthModeEx(settingEx, deviceInfo, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobSecurityLevel(setting)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobProfileImage(userBlob, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobAccessGroupID(userBlob)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceAuthGroupID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserUpdate(user)))
		return sdkResult;

	user.numFingers = 0;
	user.numCards = 0;
	user.numFaces = 0;

	if (card)
	{
		userBlob.cardObjs = new BS2CSNCard;
		memcpy(userBlob.cardObjs, card, sizeof(BS2CSNCard));
		user.numCards++;
	}
	else
	{
		if (BS_SDK_SUCCESS != (sdkResult = getUserBlobCardInfo(userBlob, id, deviceInfo)))
			return sdkResult;
	}

	if (finger)
	{
		userBlob.fingerObjs = new BS2Fingerprint;
		memcpy(userBlob.fingerObjs, finger, sizeof(BS2Fingerprint));
		user.numFingers++;
	}
	else
	{
		if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFingerprintInfo(userBlob, id, deviceInfoEx)))
			return sdkResult;
	}

	if (face)
	{
		userBlob.faceObjs = new BS2Face;
		memcpy(userBlob.faceObjs, face, sizeof(BS2Face));
		user.numFaces++;
	}
	else if (faceEx)
	{
		userBlob.faceExObjs = new BS2FaceEx;
		memcpy(userBlob.faceExObjs, faceEx, sizeof(BS2FaceEx));
		user.numFaces++;
	}
	else
	{
		if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceInfo(userBlob, id, deviceInfoEx)))
			return sdkResult;
	}

	sdkResult = BS2_EnrollUserFaceEx(context_, id, &userBlob, 1, 1);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_EnrollUserFaceEx call failed: %d", sdkResult);

	if (userBlob.cardObjs)
		delete[] userBlob.cardObjs;

	if (userBlob.fingerObjs)
		delete[] userBlob.fingerObjs;

	if (userBlob.faceObjs)
		delete[] userBlob.faceObjs;

	if (userBlob.faceExObjs)
	{
		//for (uint32_t index = 0; index < user.numFaces; index++)
		//{
		//	if (userBlob.faceExObjs[index].flag == 0)	// raw image
				delete[] userBlob.faceExObjs;
		//}
	}

	return sdkResult;
}

int UserControl::enrollUserFaceExScanAndLoad(BS2_DEVICE_ID id)
{
	BS2SimpleDeviceInfo deviceInfo = { 0, };
	BS2SimpleDeviceInfoEx deviceInfoEx = { 0, };

	int sdkResult = BS2_GetDeviceInfoEx(context_, id, &deviceInfo, &deviceInfoEx);
	if (BS_SDK_SUCCESS != sdkResult)
	{
		TRACE("BS2_GetDeviceInfoEx call failed: %d", sdkResult);
		return sdkResult;
	}

	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	BS2UserFaceExBlob userBlob = { 0, };
	BS2User& user = userBlob.user;
	BS2UserSetting& setting = userBlob.setting;
	BS2UserSettingEx& settingEx = userBlob.settingEx;
	userBlob.user_photo_obj = NULL;
	stringstream msg;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserName(userBlob, deviceInfo)))
		return sdkResult;

	getUserBlobExpiryDate(setting);

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPINCode(userBlob, deviceInfo)))
		return sdkResult;

	setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
	setting.cardAuthMode = BS2_AUTH_MODE_NONE;
	setting.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthMode(setting, deviceInfo, deviceInfoEx)))
		return sdkResult;

	settingEx.faceAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.fingerprintAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.cardAuthMode = BS2_AUTH_MODE_NONE;
	settingEx.idAuthMode = BS2_AUTH_MODE_NONE;
	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobPrivateAuthModeEx(settingEx, deviceInfo, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobSecurityLevel(setting)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobProfileImage(userBlob, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobAccessGroupID(userBlob)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceAuthGroupID(user)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobUserUpdate(user)))
		return sdkResult;

	user.numFingers = 0;
	user.numCards = 0;
	user.numFaces = 0;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobCardInfo(userBlob, id, deviceInfo)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFingerprintInfo(userBlob, id, deviceInfoEx)))
		return sdkResult;

	if (BS_SDK_SUCCESS != (sdkResult = getUserBlobFaceInfoEx(userBlob, id, deviceInfoEx)))
		return sdkResult;

	sdkResult = BS2_EnrollUserFaceEx(context_, id, &userBlob, 1, 1);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_EnrollUserFaceEx call failed: %d", sdkResult);

	if (userBlob.cardObjs)
		delete[] userBlob.cardObjs;

	if (userBlob.fingerObjs)
		delete[] userBlob.fingerObjs;

	if (userBlob.faceObjs)
		delete[] userBlob.faceObjs;

	if (userBlob.faceExObjs)
	{
		//for (uint32_t index = 0; index < user.numFaces; index++)
		//{
		//	if (userBlob.faceExObjs[index].flag == 0)	// raw image
		delete[] userBlob.faceExObjs;
		//}
	}

	return sdkResult;
}

int UserControl::getLastFingerprintImage(BS2_DEVICE_ID id, uint8_t** imageObj, uint32_t* width, uint32_t* height)
{
	int sdkResult = BS2_GetLastFingerprintImage(context_, id, imageObj, width, height);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_GetLastFingerprintImage call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::removeUser(BS2_DEVICE_ID id)
{
	string uid;
	uid.resize(BS2_USER_ID_SIZE);
	uid = Utility::getInput<string>("Please enter a user ID :");
	if (BS2_USER_ID_SIZE < uid.size())
	{
		TRACE("User ID is too big.");
		return BS_SDK_ERROR_INVALID_PARAM;
	}

	int sdkResult = BS2_RemoveUser(context_, id, const_cast<char*>(uid.c_str()), 1);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_RemoveUser call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::removeAllUser(BS2_DEVICE_ID id)
{
	int sdkResult = BS2_RemoveAllUser(context_, id);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_RemoveAllUser call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::updateCardTypeCRC(BS2SmartCardData& card)
{
	uint16_t crc = 0xFFFF;
	int sdkResult = BS2_ComputeCRC16CCITT((unsigned char*)&card.header.cardType, sizeof(BS2SmartCardData) - offsetof(BS2SmartCardHeader, cardType), &crc);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_ComputeCRC16CCITT call failed: %d", sdkResult);

	card.header.cardCRC = crc;

	return sdkResult;
}

int UserControl::updateCardCRC(BS2SmartCardData& card)
{
	uint16_t crc = 0xFFFF;
	int sdkResult = BS2_ComputeCRC16CCITT((unsigned char*)&card.header.cardCRC, sizeof(BS2SmartCardHeader) - offsetof(BS2SmartCardHeader, cardCRC), &crc);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_ComputeCRC16CCITT call failed: %d", sdkResult);

	card.header.hdrCRC = crc;

	return sdkResult;
}

int UserControl::getPinCode(string plainText, uint8_t* cipherText)
{
	int sdkResult = BS2_MakePinCode(context_, const_cast<char*>(plainText.c_str()), cipherText);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_MakePinCode call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::scanTemplate(BS2_DEVICE_ID id, uint8_t* fpTemplate)
{
	if (!fpTemplate)
		return BS_SDK_ERROR_INVALID_PARAM;

	BS2Fingerprint finger = { 0, };
RESCAN:
	int sdkResult = BS2_ScanFingerprint(context_, id, &finger, 0, BS2_FINGER_TEMPLATE_QUALITY_HIGHEST, BS2_FINGER_TEMPLATE_FORMAT_SUPREMA, onReadyToScan);
	if (BS_SDK_ERROR_EXTRACTION_LOW_QUALITY == sdkResult)
	{
		TRACE("Low quality fingerprints were scanned.");
		TRACE("Scan again.");
		goto RESCAN;
	}
	else if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_ScanFingerprint call failed: %d", sdkResult);
	else
		memcpy(fpTemplate, finger.data[0], BS2_FINGER_TEMPLATE_SIZE);

	return sdkResult;
}

int UserControl::scanCard(BS2_DEVICE_ID id, uint8_t* card)
{
	if (!card)
		return BS_SDK_ERROR_INVALID_PARAM;

	BS2Card tempCard = { 0, };
	int sdkResult = BS2_ScanCard(context_, id, &tempCard, onReadyToScan);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_ScanCard call failed: %d", sdkResult);
	else
	{
		if (tempCard.isSmartCard)
			memcpy(card, tempCard.smartCard.cardID, BS2_CARD_DATA_SIZE);
		else
			memcpy(card, tempCard.card.data, BS2_CARD_DATA_SIZE);
	}

	return sdkResult;
}

int UserControl::scanCard(BS2_DEVICE_ID id, BS2Card* card)
{
	int sdkResult = BS2_ScanCard(context_, id, card, onReadyToScan);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_ScanCard call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::writeCard(BS2_DEVICE_ID id, const BS2SmartCardData* card)
{
	int sdkResult = BS2_WriteCard(context_, id, const_cast<BS2SmartCardData*>(card));
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_WriteCard call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::eraseCard(BS2_DEVICE_ID id)
{
	int sdkResult = BS2_EraseCard(context_, id);
	if (BS_SDK_SUCCESS != sdkResult)
		TRACE("BS2_EraseCard call failed: %d", sdkResult);

	return sdkResult;
}

int UserControl::getUserBlobUserID(BS2User& user)
{
	string uid = Utility::getInput<string>("Please enter a user ID:");
	if (BS2_USER_ID_SIZE < uid.size())
	{
		TRACE("User ID is too big.");
		return BS_SDK_ERROR_INVALID_PARAM;
	}
	strcpy(user.userID, uid.c_str());

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobUserName(BS2UserBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getUserName(userBlob.user_name, deviceInfo);
}

int UserControl::getUserBlobUserName(BS2UserBlobEx& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getUserName(userBlob.user_name, deviceInfo);
}

int UserControl::getUserBlobUserName(BS2UserSmallBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getUserName(userBlob.user_name, deviceInfo);
}

int UserControl::getUserBlobUserName(BS2UserSmallBlobEx& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getUserName(userBlob.user_name, deviceInfo);
}

int UserControl::getUserBlobUserName(BS2UserFaceExBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getUserName(userBlob.user_name, deviceInfo);
}

int UserControl::getUserName(BS2_USER_NAME userName, const BS2SimpleDeviceInfo& deviceInfo)
{
	if (deviceInfo.userNameSupported)
	{
		string name = Utility::getInput<string>("Enter your name:");
		if (BS2_USER_NAME_SIZE < name.size())
		{
			TRACE("User name is too long.");
			return BS_SDK_ERROR_INVALID_PARAM;
		}
		strcpy(reinterpret_cast<char*>(userName), name.c_str());
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobExpiryDate(BS2UserSetting& setting)
{
	string inputTime = Utility::getLine("Please enter start time [YYYY-MM-DD HH:MM:SS] ?");
	BS2_TIMESTAMP startTime = Utility::convertTimeString2UTC(inputTime);
	setting.startTime = startTime;

	inputTime = Utility::getLine("Please enter end time [YYYY-MM-DD HH:MM:SS] ?");
	BS2_TIMESTAMP endTime = Utility::convertTimeString2UTC(inputTime);
	setting.endTime = endTime;

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobPINCode(BS2UserBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return makePinCode(userBlob.pin, deviceInfo);
}

int UserControl::getUserBlobPINCode(BS2UserBlobEx& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return makePinCode(userBlob.pin, deviceInfo);
}

int UserControl::getUserBlobPINCode(BS2UserSmallBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return makePinCode(userBlob.pin, deviceInfo);
}

int UserControl::getUserBlobPINCode(BS2UserSmallBlobEx& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return makePinCode(userBlob.pin, deviceInfo);
}

int UserControl::getUserBlobPINCode(BS2UserFaceExBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return makePinCode(userBlob.pin, deviceInfo);
}

int UserControl::makePinCode(BS2_USER_PIN chiperText, const BS2SimpleDeviceInfo& deviceInfo)
{
	int sdkResult = BS_SDK_SUCCESS;

	if (deviceInfo.pinSupported)
	{
		string pinString = Utility::getInput<string>("Enter the PIN code:");
		if (BS2_USER_PIN_SIZE < pinString.size())
		{
			TRACE("PIN code is too long");
			return BS_SDK_ERROR_INVALID_PARAM;
		}

		sdkResult = BS2_MakePinCode(context_, const_cast<char*>(pinString.c_str()), chiperText);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_MakePinCode call failed: %d", sdkResult);
			return sdkResult;
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobPrivateAuthMode(BS2UserSetting& setting, const BS2SimpleDeviceInfo& deviceInfo, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	stringstream msg;
	char select = Utility::getInput<char>("Do you want to register private auth mode? [y/n]");
	if (select == 'y' || select == 'Y')
	{
		if (fingerScanSupported || faceScanSupported)
		{
			msg.str("");
			msg << "Enter the biometric authentication mode" << endl;
			msg << " 0: Not use" << endl;
			msg << " 1: Biometric only" << endl;
			msg << " 2: Biometric+PIN" << endl;
			msg << " >>";
			int fingerAuthMode = Utility::getInput<int>(msg.str());
			switch (fingerAuthMode)
			{
			case 1:
				setting.fingerAuthMode = BS2_AUTH_MODE_BIOMETRIC_ONLY;
				break;
			case 2:
				setting.fingerAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_BIOMETRIC_PIN : BS2_AUTH_MODE_BIOMETRIC_ONLY;
				break;
			default:
				setting.fingerAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}

		if (deviceInfo.cardSupported)
		{
			msg.str("");
			msg << "Enter the card authentication mode" << endl;
			msg << " 0: Not use" << endl;
			msg << " 1: Card only" << endl;
			msg << " 2: Card+Biometric" << endl;
			msg << " 3: Card+PIN" << endl;
			msg << " 4: Card+(Biometric/PIN)" << endl;
			msg << " 5: Card+Biometric+PIN" << endl;
			msg << " >>";
			int cardAuthMode = Utility::getInput<int>(msg.str());
			switch (cardAuthMode)
			{
			case 1:
				setting.cardAuthMode = BS2_AUTH_MODE_CARD_ONLY;
				break;
			case 2:
				setting.cardAuthMode = (fingerScanSupported || faceScanSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC : BS2_AUTH_MODE_CARD_ONLY;
				break;
			case 3:
				setting.cardAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_CARD_PIN : BS2_AUTH_MODE_CARD_ONLY;
				break;
			case 4:
				setting.cardAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC_OR_PIN : BS2_AUTH_MODE_CARD_ONLY;
				break;
			case 5:
				setting.cardAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_CARD_BIOMETRIC_PIN : BS2_AUTH_MODE_CARD_ONLY;
				break;
			default:
				setting.cardAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}

		{
			msg.str("");
			msg << "Enter the ID authentication mode" << endl;
			msg << " 0: Not use" << endl;
			msg << " 1: ID+Biometric" << endl;
			msg << " 2: ID+PIN" << endl;
			msg << " 3: ID+(Biometric/PIN)" << endl;
			msg << " 4: ID+Biometric+PIN" << endl;
			msg << " >>";
			int idAuthMode = Utility::getInput<int>(msg.str());
			switch (idAuthMode)
			{
			case 1:
				setting.idAuthMode = (fingerScanSupported || faceScanSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC : BS2_AUTH_MODE_NONE;
				break;
			case 2:
				setting.idAuthMode = deviceInfo.pinSupported ? BS2_AUTH_MODE_ID_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 3:
				setting.idAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 4:
				setting.idAuthMode = (fingerScanSupported || faceScanSupported || deviceInfo.pinSupported) ? BS2_AUTH_MODE_ID_BIOMETRIC_PIN : BS2_AUTH_MODE_NONE;
				break;
			default:
				setting.idAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobPrivateAuthModeEx(BS2UserSettingEx& settingEx, const BS2SimpleDeviceInfo& deviceInfo, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;

	stringstream msg;
	char select = Utility::getInput<char>("Do you want to register private auth-ex mode? [y/n]");
	if (select == 'y' || select == 'Y')
	{
		if (faceExScanSupported)
		{
			msg.str("");
			msg << "Enter the FaceEx authentication mode" << endl;
			msg << " 0: Not use" << endl;
			msg << " 1: Face" << endl;
			msg << " 2: Face + Fingerprint" << endl;
			msg << " 3: Face + PIN" << endl;
			msg << " 4: Face + Fingerprint / PIN" << endl;
			msg << " 5: Face + Fingerprint + PIN" << endl;
			msg << " >>";
			int faceAuthMode = Utility::getInput<int>(msg.str());
			switch (faceAuthMode)
			{
			case 1:
				settingEx.faceAuthMode = BS2_EXT_AUTH_MODE_FACE_ONLY;
				break;
			case 2:
				settingEx.faceAuthMode = fingerScanSupported ? BS2_EXT_AUTH_MODE_FACE_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 3:
				settingEx.faceAuthMode = deviceInfo.pinSupported ? BS2_EXT_AUTH_MODE_FACE_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 4:
				settingEx.faceAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_FACE_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 5:
				settingEx.faceAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_FACE_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			default:
				settingEx.faceAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}

		if (fingerScanSupported)
		{
			msg.str("");
			msg << "Enter the Fingerprint authentication mode\n";
			msg << " 0: Not use" << endl;
			msg << " 1: Fingerprint" << endl;
			msg << " 2: Fingerprint + Face" << endl;
			msg << " 3: Fingerprint + PIN" << endl;
			msg << " 4: Fingerprint + Face/PIN" << endl;
			msg << " 5: Fingerprint + Face + PIN" << endl;
			msg << " >>";
			int fingerAuthMode = Utility::getInput<int>(msg.str());
			switch (fingerAuthMode)
			{
			case 1:
				settingEx.fingerprintAuthMode = BS2_EXT_AUTH_MODE_FINGERPRINT_ONLY;
				break;
			case 2:
				settingEx.fingerprintAuthMode = faceExScanSupported ? BS2_EXT_AUTH_MODE_FINGERPRINT_FACE : BS2_AUTH_MODE_NONE;
				break;
			case 3:
				settingEx.fingerprintAuthMode = deviceInfo.pinSupported ? BS2_EXT_AUTH_MODE_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 4:
				settingEx.fingerprintAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_FINGERPRINT_FACE_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 5:
				settingEx.fingerprintAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_FINGERPRINT_FACE_PIN : BS2_AUTH_MODE_NONE;
				break;
			default:
				settingEx.fingerprintAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}

		if (deviceInfo.cardSupported)
		{
			msg.str("");
			msg << "Enter the Card authentication mode\n";
			msg << " 0: Not use" << endl;
			msg << " 1: Card" << endl;
			msg << " 2: Card + Face" << endl;
			msg << " 3: Card + Fingerprint" << endl;
			msg << " 4: Card + PIN" << endl;
			msg << " 5: Card + Face/Fingerprint" << endl;
			msg << " 6: Card + Face/PIN" << endl;
			msg << " 7: Card + Fingerprint/PIN" << endl;
			msg << " 8: Card + Face/Fingerprint/PIN" << endl;
			msg << " 9: Card + Face + Fingerprint" << endl;
			msg << "10: Card + Face + PIN" << endl;
			msg << "11: Card + Fingerprint + Face" << endl;
			msg << "12: Card + Fingerprint + PIN" << endl;
			msg << "13: Card + Face/Fingerprint + PIN" << endl;
			msg << "14: Card + Face + Fingerprint/PIN" << endl;
			msg << "15: Card + Fingerprint + Face/PIN" << endl;
			msg << " >>";
			int cardAuthMode = Utility::getInput<int>(msg.str());
			switch (cardAuthMode)
			{
			case 1:
				settingEx.cardAuthMode = BS2_EXT_AUTH_MODE_CARD_ONLY;
				break;
			case 2:
				settingEx.cardAuthMode = faceExScanSupported ? BS2_EXT_AUTH_MODE_CARD_FACE : BS2_AUTH_MODE_NONE;
				break;
			case 3:
				settingEx.cardAuthMode = fingerScanSupported ? BS2_EXT_AUTH_MODE_CARD_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 4:
				settingEx.cardAuthMode = deviceInfo.pinSupported ? BS2_EXT_AUTH_MODE_CARD_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 5:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_OR_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 6:
				settingEx.cardAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 7:
				settingEx.cardAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 8:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_OR_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 9:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 10:
				settingEx.cardAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 11:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_CARD_FINGERPRINT_FACE : BS2_AUTH_MODE_NONE;
				break;
			case 12:
				settingEx.cardAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 13:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_OR_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 14:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FACE_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 15:
				settingEx.cardAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_CARD_FINGERPRINT_FACE_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			default:
				settingEx.cardAuthMode = BS2_AUTH_MODE_NONE;
			}
		}	// cardAuthMode

		{
			msg.str("");
			msg << "Enter the ID authentication mode\n";
			msg << " 0: Not use" << endl;
			msg << " 1: ID + Face" << endl;
			msg << " 2: ID + Fingerprint" << endl;
			msg << " 3: ID + PIN" << endl;
			msg << " 4: ID + Face/Fingerprint" << endl;
			msg << " 5: ID + Face/PIN" << endl;
			msg << " 6: ID + Fingerprint/PIN" << endl;
			msg << " 7: ID + Face/Fingerprint/PIN" << endl;
			msg << " 8: ID + Face + Fingerprint" << endl;
			msg << " 9: ID + Face + PIN" << endl;
			msg << "10: ID + Fingerprint + Face" << endl;
			msg << "11: ID + Fingerprint + PIN" << endl;
			msg << "12: ID + Face/Fingerprint + PIN" << endl;
			msg << "13: ID + Face + Fingerprint/PIN" << endl;
			msg << "14: ID + Fingerprint + Face/PIN" << endl;
			msg << " >>";
			int idAuthMode = Utility::getInput<int>(msg.str());
			switch (idAuthMode)
			{
			case 1:
				settingEx.idAuthMode = faceExScanSupported ? BS2_EXT_AUTH_MODE_ID_FACE : BS2_AUTH_MODE_NONE;
				break;
			case 2:
				settingEx.idAuthMode = fingerScanSupported ? BS2_EXT_AUTH_MODE_ID_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 3:
				settingEx.idAuthMode = deviceInfo.pinSupported ? BS2_EXT_AUTH_MODE_ID_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 4:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_OR_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 5:
				settingEx.idAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 6:
				settingEx.idAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 7:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_OR_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 8:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_FINGERPRINT : BS2_AUTH_MODE_NONE;
				break;
			case 9:
				settingEx.idAuthMode = (faceExScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 10:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported) ? BS2_EXT_AUTH_MODE_ID_FINGERPRINT_FACE : BS2_AUTH_MODE_NONE;
				break;
			case 11:
				settingEx.idAuthMode = (fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 12:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_OR_FINGERPRINT_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 13:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FACE_FINGERPRINT_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			case 14:
				settingEx.idAuthMode = (faceExScanSupported && fingerScanSupported && deviceInfo.pinSupported) ? BS2_EXT_AUTH_MODE_ID_FINGERPRINT_FACE_OR_PIN : BS2_AUTH_MODE_NONE;
				break;
			default:
				settingEx.idAuthMode = BS2_AUTH_MODE_NONE;
				break;
			}
		}	// idAuthMode
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobSecurityLevel(BS2UserSetting& setting)
{
	stringstream msg;
	msg << "Enter the security level for this user" << endl;
	msg << "[0: Default, 1: Lower, 2: Low, 3: Normal, 4: High, 5, Higher]";
	int securityLevel = Utility::getInput<int>(msg.str());
	switch (securityLevel)
	{
	case BS2_USER_SECURITY_LEVEL_DEFAULT:
	case BS2_USER_SECURITY_LEVEL_LOWER:
	case BS2_USER_SECURITY_LEVEL_LOW:
	case BS2_USER_SECURITY_LEVEL_NORMAL:
	case BS2_USER_SECURITY_LEVEL_HIGH:
	case BS2_USER_SECURITY_LEVEL_HIGHER:
		setting.securityLevel = securityLevel;
		break;
	default:
		setting.securityLevel = BS2_USER_SECURITY_LEVEL_DEFAULT;
		break;
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobProfileImage(BS2UserSmallBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getProfileImage(&userBlob.user_photo_obj, deviceInfo);
}

int UserControl::getUserBlobProfileImage(BS2UserSmallBlobEx& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getProfileImage(&userBlob.user_photo_obj, deviceInfo);
}

int UserControl::getUserBlobProfileImage(BS2UserFaceExBlob& userBlob, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getProfileImage(&userBlob.user_photo_obj, deviceInfo);
}

int UserControl::getProfileImage(BS2UserPhoto** photo, const BS2SimpleDeviceInfo& deviceInfo)
{
	stringstream msg;

	if (deviceInfo.userPhotoSupported)
	{
		char profileImage = Utility::getInput<char>("Do you want to register a profile image? [y/n]");
		if ('y' == profileImage || 'Y' == profileImage)
		{
			string imagePath = Utility::getInput<string>("Enter the profile image path and name:");
			uint32_t size = Utility::getResourceSize(imagePath);
			shared_ptr<uint8_t> buffer(new uint8_t[size], ArrayDeleter<uint8_t>());

			while (BS2_USER_PHOTO_SIZE < size)
			{
				msg.str("");
				msg << "Image is to big.\n";
				msg << "Re-enter an image smaller than 16384 byte:";
				imagePath = Utility::getInput<string>(msg.str());
				size = Utility::getResourceSize(imagePath);
			}

			if (Utility::getResourceFromFile(imagePath, buffer, size))
			{
				*photo = new BS2UserPhoto;
				(*photo)->size = size;
				memcpy((*photo)->data, buffer.get(), size);
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobAccessGroupID(BS2UserBlob& userBlob)
{
	return getAccessGroupID(userBlob.accessGroupId);
}

int UserControl::getUserBlobAccessGroupID(BS2UserBlobEx& userBlob)
{
	return getAccessGroupID(userBlob.accessGroupId);
}

int UserControl::getUserBlobAccessGroupID(BS2UserSmallBlob& userBlob)
{
	return getAccessGroupID(userBlob.accessGroupId);
}

int UserControl::getUserBlobAccessGroupID(BS2UserSmallBlobEx& userBlob)
{
	return getAccessGroupID(userBlob.accessGroupId);
}

int UserControl::getUserBlobAccessGroupID(BS2UserFaceExBlob& userBlob)
{
	return getAccessGroupID(userBlob.accessGroupId);
}

int UserControl::getAccessGroupID(BS2_ACCESS_GROUP_ID* accessGroupId)
{
	char flag = Utility::getInput<char>("Do you want to register access group ID? [y/n]");
	if ('y' == flag || 'Y' == flag)
	{
		stringstream msg;
		msg << "Please enter access group IDs. ex)ID1 ID2 ID3 ...\n";
		string inStrAGID = Utility::getLine(msg.str());
		if (0 == inStrAGID.size())
			return BS_SDK_ERROR_CANNOT_FIND_ACCESS_GROUP;

		vector<string> listID = Utility::tokenizeString(inStrAGID);
		if (listID.size() < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER)
		{
			uint32_t index(0);
			for (auto groupID : listID)
			{
				BS2_ACCESS_GROUP_ID gid = atoi(groupID.c_str());
				accessGroupId[index++] = gid;
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobFaceAuthGroupID(BS2User& user)
{
	stringstream msg;
	msg << "Please enter a authentication group ID.\n";
	msg << "This is used for face authentication. [0: Not using]";
	uint32_t authGroupID = Utility::getInput<uint32_t>(msg.str());
	user.authGroupID = authGroupID;

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobUserUpdate(BS2User& user)
{
	char flag = Utility::getInput<char>("Do you want to overwrite the user if it exist? [y/n]");
	user.flag = (flag == 'y' || flag == 'Y') ? BS2_USER_FLAG_CREATED | BS2_USER_FLAG_UPDATED : BS2_USER_FLAG_CREATED;

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobCardInfo(BS2UserBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getCardInfo(&userBlob.cardObjs, userBlob.user.numCards, id, deviceInfo);
}

int UserControl::getUserBlobCardInfo(BS2UserBlobEx& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getCardInfo(&userBlob.cardObjs, userBlob.user.numCards, id, deviceInfo);
}

int UserControl::getUserBlobCardInfo(BS2UserSmallBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getCardInfo(&userBlob.cardObjs, userBlob.user.numCards, id, deviceInfo);
}

int UserControl::getUserBlobCardInfo(BS2UserSmallBlobEx& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getCardInfo(&userBlob.cardObjs, userBlob.user.numCards, id, deviceInfo);
}

int UserControl::getUserBlobCardInfo(BS2UserFaceExBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	return getCardInfo(&userBlob.cardObjs, userBlob.user.numCards, id, deviceInfo);
}

int UserControl::getCardInfo(BS2CSNCard** cardObjs, uint8_t& numOfCards, BS2_DEVICE_ID id, const BS2SimpleDeviceInfo& deviceInfo)
{
	int sdkResult = BS_SDK_SUCCESS;

	if (deviceInfo.cardSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan card? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numCard = Utility::getInput<uint32_t>("How many cards would you like to register?");
			BS2CSNCard* ptrCard = new BS2CSNCard[numCard];
			if (ptrCard)
			{
				*cardObjs = ptrCard;
				for (uint32_t index = 0; index < numCard;)
				{
					BS2Card card = { 0, };
					sdkResult = BS2_ScanCard(context_, id, &card, onReadyToScan);
					if (BS_SDK_SUCCESS != sdkResult)
						TRACE("BS2_ScanCard call failed: %d", sdkResult);
					else
					{
						if (card.isSmartCard)
						{
							TRACE("CSN card only supported.");
						}
						else
						{
							memcpy(&ptrCard[index], &card.card, sizeof(BS2CSNCard));
						}
						numOfCards++;
						index++;
					}
				}
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobFingerprintInfo(BS2UserBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	return getFingerprintInfo(&userBlob.fingerObjs, userBlob.user.numFingers, id, deviceInfoEx);
}

int UserControl::getUserBlobFingerprintInfo(BS2UserBlobEx& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	return getFingerprintInfo(&userBlob.fingerObjs, userBlob.user.numFingers, id, deviceInfoEx);
}

int UserControl::getUserBlobFingerprintInfo(BS2UserSmallBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	return getFingerprintInfo(&userBlob.fingerObjs, userBlob.user.numFingers, id, deviceInfoEx);
}

int UserControl::getUserBlobFingerprintInfo(BS2UserSmallBlobEx& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	return getFingerprintInfo(&userBlob.fingerObjs, userBlob.user.numFingers, id, deviceInfoEx);
}

int UserControl::getUserBlobFingerprintInfo(BS2UserFaceExBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	return getFingerprintInfo(&userBlob.fingerObjs, userBlob.user.numFingers, id, deviceInfoEx);
}

int UserControl::getFingerprintInfo(BS2Fingerprint** fingerObjs, uint8_t& numOfFingers, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	bool fingerScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FINGER_SCAN;
	int sdkResult = BS_SDK_SUCCESS;

	if (fingerScanSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan fingerprint? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFinger = Utility::getInput<uint32_t>("How many fingers would you like to register?");
			BS2Fingerprint* ptrFinger = new BS2Fingerprint[numFinger];
			if (ptrFinger)
			{
				*fingerObjs = ptrFinger;
				for (uint32_t index = 0; index < numFinger; index++)
				{
					for (uint32_t templateIndex = 0; templateIndex < BS2_TEMPLATE_PER_FINGER;)
					{
						sdkResult = BS2_ScanFingerprint(context_, id, &ptrFinger[index], templateIndex, BS2_FINGER_TEMPLATE_QUALITY_HIGHEST, BS2_FINGER_TEMPLATE_FORMAT_SUPREMA, onReadyToScan);
						if (BS_SDK_SUCCESS != sdkResult)
							TRACE("BS2_ScanFingerprint call failed: %d", sdkResult);
						else
							templateIndex++;
					}
					numOfFingers++;
				}
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobFaceInfo(BS2UserSmallBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	BS2User& user = userBlob.user;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;
	int sdkResult = BS_SDK_SUCCESS;

	if (faceScanSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan face from device? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFace = Utility::getInput<uint32_t>("How many face would you like to register?");
			BS2Face* ptrFace = new BS2Face[numFace];
			if (ptrFace)
			{
				userBlob.faceObjs = ptrFace;
				for (uint32_t index = 0; index < numFace; index++)
				{
					sdkResult = scanFace(id, &ptrFace[index], user.numFaces);
					if (BS_SDK_SUCCESS != sdkResult)
						return sdkResult;
				}
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobFaceInfo(BS2UserFaceExBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	BS2User& user = userBlob.user;
	bool faceScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_SCAN;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;
	int sdkResult = BS_SDK_SUCCESS;

	if (faceScanSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan face from device? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFace = Utility::getInput<uint32_t>("How many face would you like to register?");
			BS2Face* ptrFace = new BS2Face[numFace];
			if (ptrFace)
			{
				userBlob.faceObjs = ptrFace;
				for (uint32_t index = 0; index < numFace; index++)
				{
					sdkResult = scanFace(id, &ptrFace[index], user.numFaces);
					if (BS_SDK_SUCCESS != sdkResult)
						return sdkResult;
				}
			}
		}
	}
	else if (faceExScanSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan faceEx from device? [y/n]");
		if ('y' == flag || 'Y' == flag)
		{
			uint32_t numFace = Utility::getInput<uint32_t>("How many faceEx would you like to register?");
			BS2FaceEx* ptrFace = new BS2FaceEx[numFace];
			if (ptrFace)
			{
				userBlob.faceExObjs = ptrFace;
				for (uint32_t index = 0; index < numFace; index++)
				{
					sdkResult = scanFaceEx(id, &ptrFace[index], user.numFaces);
					if (BS_SDK_SUCCESS != sdkResult)
						return sdkResult;
				}
			}
		}
		else
		{
			flag = Utility::getInput<char>("Do you want to register from file image? [y/n]");
			if ('y' == flag || 'Y' == flag)
			{
				string imagePath = Utility::getInput<string>("Enter the face image path and name:");
				uint32_t size = Utility::getResourceSize(imagePath);
				shared_ptr<uint8_t> buffer(new uint8_t[size], ArrayDeleter<uint8_t>());

				size_t dataOffset = offsetof(BS2FaceEx, rawImageData);
				size_t faceSize = dataOffset + size;
				if (Utility::getResourceFromFile(imagePath, buffer, size))
				{
					uint8_t* ptrFace = new uint8_t[faceSize];
					memset(ptrFace, 0x0, faceSize);
					userBlob.faceExObjs = reinterpret_cast<BS2FaceEx*>(ptrFace);
					userBlob.faceExObjs->flag = 0;
					userBlob.faceExObjs->imageLen = size;
					memcpy(ptrFace + dataOffset, buffer.get(), size);

					user.numFaces = 1;
				}
			}
		}
	}

	return BS_SDK_SUCCESS;
}

int UserControl::getUserBlobFaceInfoEx(BS2UserFaceExBlob& userBlob, BS2_DEVICE_ID id, const BS2SimpleDeviceInfoEx& deviceInfoEx)
{
	BS2User& user = userBlob.user;
	bool faceExScanSupported = (deviceInfoEx.supported & BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN) == BS2SimpleDeviceInfoEx::BS2_SUPPORT_FACE_EX_SCAN;
	int sdkResult = BS_SDK_SUCCESS;
	uint32_t numOfScan(0), numOfImage(0);

	if (faceExScanSupported)
	{
		char flag = Utility::getInput<char>("Do you want to scan faceEx from device? [y/n]");
		if ('y' == flag || 'Y' == flag)
			numOfScan = Utility::getInput<uint32_t>("How many faceEx would you like to register?");

		flag = Utility::getInput<char>("Do you want to register from file image? [y/n]");
		if ('y' == flag || 'Y' == flag)
			numOfImage = Utility::getInput<uint32_t>("How many image files would you like to register?");

		if (0 < numOfScan || 0 < numOfImage)
			sdkResult = scanAndLoadFaceEx(userBlob, id, numOfScan, numOfImage);
	}

	return sdkResult;
}

int UserControl::scanAndLoadFaceEx(BS2UserFaceExBlob& userBlob, BS2_DEVICE_ID id, uint32_t numOfScan, uint32_t numOfImage)
{
	BS2User& user = userBlob.user;
	int sdkResult = BS_SDK_SUCCESS;

	uint32_t sizeTotal = numOfScan * sizeof(BS2FaceEx);
	vector<UserImageInfo> listImage;
	const size_t dataOffset = offsetof(BS2FaceEx, rawImageData);

	for (uint32_t imageIdx = 0; imageIdx < numOfImage; imageIdx++)
	{
		string filePath = Utility::getInput<string>("Enter the face image path and name:");
		uint32_t size = Utility::getResourceSize(filePath);
		if (0 < size)
		{
			sizeTotal += (dataOffset + size);
			UserImageInfo info;
			info.fileName = filePath;
			info.size = size;
			listImage.push_back(info);
		}
	}

	uint8_t* ptrBuff = new uint8_t[sizeTotal];
	memset(ptrBuff, 0x0, sizeTotal);
	userBlob.faceExObjs = reinterpret_cast<BS2FaceEx*>(ptrBuff);
	BS2FaceEx* ptrFace = userBlob.faceExObjs;

	for (uint32_t idx = 0; idx < numOfScan; idx++, ptrFace++)
	{
		sdkResult = scanFaceEx(id, ptrFace, user.numFaces);
		if (BS_SDK_SUCCESS != sdkResult)
			return sdkResult;
	}

	for (auto imageInfo : listImage)
	{
		shared_ptr<uint8_t> buffer(new uint8_t[imageInfo.size], ArrayDeleter<uint8_t>());
		uint32_t sizeFaceEx = dataOffset + imageInfo.size;
		if (Utility::getResourceFromFile(imageInfo.fileName, buffer, imageInfo.size))
		{
			ptrFace->flag = BS2_FACE_EX_FLAG_NONE;
			ptrFace->imageLen = imageInfo.size;
			memcpy(&ptrFace->rawImageData, buffer.get(), imageInfo.size);
			user.numFaces++;

			ptrBuff = reinterpret_cast<uint8_t*>(ptrFace);
			ptrBuff += sizeFaceEx;
			ptrFace = reinterpret_cast<BS2FaceEx*>(ptrBuff);
		}
	}

	return sdkResult;
}

int UserControl::scanFace(BS2_DEVICE_ID id, BS2Face* ptrFace, uint8_t& numOfFace)
{
	if (!ptrFace)
		return BS_SDK_ERROR_INVALID_PARAM;

	const int MAX_RETRY = 2;
	int sdkResult = BS_SDK_SUCCESS;
	int retryCount = 0;

	while (retryCount < MAX_RETRY)
	{
		sdkResult = BS2_ScanFace(context_, id, ptrFace, BS2_FACE_ENROLL_THRESHOLD_DEFAULT, onReadyToScan);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_ScanFace call failed: %d", sdkResult);
			retryCount++;
		}
		else
		{
			numOfFace++;
			break;
		}
	}

	return sdkResult;
}

int UserControl::scanFaceEx(BS2_DEVICE_ID id, BS2FaceEx* ptrFace, uint8_t& numOfFace)
{
	if (!ptrFace)
		return BS_SDK_ERROR_INVALID_PARAM;

	const int MAX_RETRY = 2;
	int sdkResult = BS_SDK_SUCCESS;
	int retryCount = 0;

	while (retryCount < MAX_RETRY)
	{
		sdkResult = BS2_ScanFaceEx(context_, id, ptrFace, BS2_FACE_ENROLL_THRESHOLD_DEFAULT, onReadyToScan);
		if (BS_SDK_SUCCESS != sdkResult)
		{
			TRACE("BS2_ScanFaceEx call failed: %d", sdkResult);
			retryCount++;
		}
		else
		{
			numOfFace++;
			break;
		}
	}

	return sdkResult;
}

void UserControl::dumpHexa(const uint8_t* data, uint32_t size)
{
	if (NULL == data || size == 0)
		return;

	TRACE("---2---4---6---8--10--12--14--16--18--20--22--24--26--28--30--32--34--36--38--40");
	for (uint32_t index = 0; index < size; index++)
		printf("%02x", data[index]);
	cout << endl;
}

void UserControl::print(const BS2UserBlob& userBlob)
{
	TRACE("==[BS2UserBlob]==");
	print(userBlob.user);
	print(userBlob.setting);
	TRACE("user_name:%s", userBlob.user_name);
	print(userBlob.user_photo);
	print(userBlob.cardObjs, userBlob.user.numCards);
	print(userBlob.fingerObjs, userBlob.user.numFingers);
	print(userBlob.faceObjs, userBlob.user.numFaces);
	for (int i = 0; i < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER; i++)
	{
		TRACE("[%d] accessGroupId:%u", i, userBlob.accessGroupId[i]);
	}
}

void UserControl::print(const BS2UserFaceExBlob& userBlob)
{
	TRACE("==[BS2UserFaceExBlob]==");
	print(userBlob.user);
	print(userBlob.setting);
	TRACE("user_name:%s", userBlob.user_name);
	if (userBlob.user_photo_obj)
		print(*userBlob.user_photo_obj);
	if (userBlob.cardObjs && 0 < userBlob.user.numCards)
		print(userBlob.cardObjs, userBlob.user.numCards);
	if (userBlob.fingerObjs && 0 < userBlob.user.numFingers)
		print(userBlob.fingerObjs, userBlob.user.numFingers);
	if (userBlob.faceExObjs && 0 < userBlob.user.numFaces)
		print(userBlob.faceExObjs, userBlob.user.numFaces);
	print(userBlob.settingEx);
	for (int i = 0; i < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER; i++)
	{
		TRACE("[%d] accessGroupId:%u", i, userBlob.accessGroupId[i]);
	}
}

void UserControl::printHeader(const BS2UserBlob& userBlob)
{
	TRACE("==[BS2UserBlob Header]==");
	print(userBlob.user);
	print(userBlob.setting);
	TRACE("user_name:%s", userBlob.user_name);
	print(userBlob.user_photo);
	for (int i = 0; i < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER; i++)
	{
		TRACE("[%d] accessGroupId:%u", i, userBlob.accessGroupId[i]);
	}
}

void UserControl::printHeaderFaceEx(const BS2UserFaceExBlob& userBlob)
{
	TRACE("==[BS2UserFaceExBlob Header]==");
	print(userBlob.user);
	print(userBlob.setting);
	print(userBlob.settingEx);
	TRACE("user_name:%s", userBlob.user_name);
	if (userBlob.user_photo_obj)
		print(*userBlob.user_photo_obj);
	for (int i = 0; i < BS2_MAX_NUM_OF_ACCESS_GROUP_PER_USER; i++)
	{
		TRACE("[%d] accessGroupId:%u", i, userBlob.accessGroupId[i]);
	}
}

void UserControl::print(const BS2User& user)
{
	TRACE("==[BS2User]==");
	TRACE("userID:%s", user.userID);
	TRACE("formatVersion:%u", user.formatVersion);
	TRACE("flag:%u", user.flag);
	TRACE("version:%u", user.version);
	TRACE("numCards:%u", user.numCards);
	TRACE("numFingers:%u", user.numFingers);
	TRACE("numFaces:%u", user.numFaces);
	TRACE("authGroupID:%u", user.authGroupID);
	TRACE("faceChecksum:%u", user.faceChecksum);
}

void UserControl::print(const BS2UserSetting& settings)
{
	TRACE("==[BS2UserSetting]==");
	TRACE("startTime:%u", settings.startTime);
	TRACE("endTime:%u", settings.endTime);
	TRACE("fingerAuthMode:%u", settings.fingerAuthMode);
	TRACE("cardAuthMode:%u", settings.cardAuthMode);
	TRACE("idAuthMode:%u", settings.idAuthMode);
	TRACE("securityLevel:%u", settings.securityLevel);
}

void UserControl::print(const BS2UserSettingEx& settings)
{
	TRACE("==[BS2UserSettingEx]==");
	TRACE("faceAuthMode:%u", settings.faceAuthMode);
	TRACE("fingerprintAuthMode:%u", settings.fingerprintAuthMode);
	TRACE("cardAuthMode:%u", settings.cardAuthMode);
	TRACE("idAuthMode:%u", settings.idAuthMode);
}

void UserControl::print(const BS2UserPhoto& photo)
{
	TRACE("==[BS2UserPhoto]==");
	TRACE("size:%u", photo.size);

	if (0 < photo.size)
	{
		shared_ptr<uint8_t> buffer(new uint8_t[photo.size], ArrayDeleter<uint8_t>());
		memcpy(buffer.get(), photo.data, photo.size);
		Utility::setResourceToFile("c:\\temp\\temp.jpg", buffer, photo.size);
	}
}

void UserControl::print(const BS2CSNCard* card, uint8_t numCard)
{
	TRACE("==[BS2CSNCard]==");
	for (int i = 0; i < numCard; i++)
	{
		TRACE("[%d] type:%u", i, card[i].type);
		TRACE("[%d] size:%u", i, card[i].size);
		//TRACE([%d] "data:%s", i, card[i].data);
	}
}

void UserControl::print(const BS2Fingerprint* finger, uint8_t numFinger)
{
	TRACE("==[BS2Fingerprint]==");
	for (int i = 0; i < numFinger; i++)
	{
		TRACE("[%d] index:%u", i, finger[i].index);
		TRACE("[%d] flag:%u", i, finger[i].flag);
		//TRACE("[%d] data:%s", i, finger[i].data[0]);
		//TRACE("[%d] data:%s", i, finger[i].data[1]);
	}
}

void UserControl::print(const BS2Face* face, uint8_t numFace)
{
	TRACE("==[BS2Face]==");
	for (int i = 0; i < numFace; i++)
	{
		TRACE("[%d] faceIndex:%u", i, face[i].faceIndex);
		TRACE("[%d] numOfTemplate:%u", i, face[i].numOfTemplate);
		TRACE("[%d] flag:%u", i, face[i].flag);
		TRACE("[%d] imageLen:%u", i, face[i].imageLen);

		//for (int row = 0; row < BS2_TEMPLATE_PER_FACE; row++)
		//{
		//	TRACE("[%d] %s", i, face[i].templateData[row]);
		//	//for (int col = 0; col < BS2_FACE_TEMPLATE_LENGTH; col++)
		//	//{
		//	//	//TRACE("[%d][%d] %s", i, row, face[i].templateData[row][col]);
		//	//}
		//}
	}
}

void UserControl::print(const BS2Card& card)
{
	TRACE("==[BS2Card]==");
	TRACE("isSmartCard:%s", card.isSmartCard ? "Smart" : "CSN");
	if (card.isSmartCard)
		printCardSmart(card.smartCard);
	else
		printCardCSN(card.card);
}

void UserControl::printCardSmartHeader(const BS2SmartCardHeader& header)
{
	TRACE("==[BS2SmartCardHeader]==");
	TRACE("hdrCRC:%u", header.hdrCRC);
	TRACE("cardCRC:%u", header.cardCRC);
	TRACE("cardType:%u", header.cardType);
	TRACE("numOfTemplate:%u", header.numOfTemplate);
	TRACE("templateSize:%u", header.templateSize);
	TRACE("issueCount:%u", header.issueCount);
	TRACE("duressMask:%u", header.duressMask);
	TRACE("cardAuthMode:0x%x", header.cardAuthMode);
	TRACE("useAlphanumericID:%u", header.useAlphanumericID);
}

void UserControl::printCardSmartCredential(const BS2SmartCardCredentials& cred)
{
	TRACE("==[BS2SmartCardCredentials]==");
}

void UserControl::printCardAOC(const BS2AccessOnCardData& aoc)
{
	TRACE("==[BS2AccessOnCardData]==");
	for (int i = 0; i < BS2_SMART_CARD_MAX_ACCESS_GROUP_COUNT; i++)
	{
		if (0 == aoc.accessGroupID[i])
			break;
		cout << aoc.accessGroupID[i] << ",";
	}
	cout << endl;
	TRACE("startTime:%u", aoc.startTime);
	TRACE("endTime:%u", aoc.endTime);
}

void UserControl::printCardSmart(const BS2SmartCardData& card)
{
	TRACE("==[BS2SmartCardData]==");
	printCardSmartHeader(card.header);
	dumpHexa(card.cardID, BS2_CARD_DATA_SIZE);
	printCardSmartCredential(card.credentials);
	printCardAOC(card.accessOnData);
}

void UserControl::printCardCSN(const BS2CSNCard& card)
{
	TRACE("==[BS2CSNCard]==");
	TRACE("type:%u", card.type);
	TRACE("size:%u", card.size);
	dumpHexa(card.data, BS2_CARD_DATA_SIZE);
}

void UserControl::print(const BS2FaceEx* face, uint8_t numFace)
{
	TRACE("==[BS2FaceEx]==");
	for (int i = 0; i < numFace; i++)
	{
		TRACE("[%d] faceIndex:%u", i, face[i].faceIndex);
		TRACE("[%d] numOfTemplate:%u", i, face[i].numOfTemplate);
		TRACE("[%d] flag:%u", i, face[i].flag);
		TRACE("[%d] imageLen:%u", i, face[i].imageLen);

		if (1 == face[i].flag)
		{
			//TRACE("[%d] imageData:%s", Utility::getHexaString(face[i].imageData, face[i].imageLen).c_str());
			TRACE("[%d] irImageLen:%u", i, face[i].irImageLen);
			//TRACE("[%d] irImageData:%s", Utility::getHexaString(face[i].irImageData, face[i].irImageLen).c_str());
			for (int j = 0; j < BS2_MAX_TEMPLATES_PER_FACE_EX; j++)
			{
				const BS2TemplateEx& templateEx = face[i].templateEx[j];
				TRACE("[%d][%d] templateEx.isIR:%u", i, j, templateEx.isIR);
				//TRACE("[%d][%d] templateEx.data:%s", Utility::getHexaString(templateEx.data, BS2_FACE_EX_TEMPLATE_SIZE).c_str());
			}
		}
	}
}