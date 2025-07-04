/**
 * Foxtrot Serialize
 * Version 1.0
 * Ethan MacDonald (@emd22) 2025
 */

#include <cstdio>

#include <string>
#include <assert.h>

#include <vector>
#include <iostream>

#include "FxTypes.hpp"
#include "FxHash.hpp"

#ifdef FX_USE_MEMPOOL
#define FX_ALLOC_MEM(type_, size_) FxMemPool::Alloc<type_>(size_)
#define FX_FREE_MEM(ptr_) FxMemPool::Free(ptr_)
#else
#define FX_ALLOC_MEM(type_, size_) static_cast<type_*>(malloc(size_))
#define FX_FREE_MEM(ptr_) free(ptr_)
#endif

/*
*	Structure of FXSD (FoXtrot Serialized Data)
*
*       - The first section is a "Types" section that stores IDs and sizes for
*         all types that have been serialized. This includes primitives(int, char),
*         and members structures.
*
*       - The main "Data" section immediately follows the types and contains an entry
*         per serialized value. Member structures will be serialized and written inline
*         and will be treated like another entry inside of the current one.
*
	+-------------- File Header -------------------------------------------+
	| FXSD       | int8[4] | File signature, start of types section
	| 0000 0000  | uint32  | Length of types section
	+----------------------------------------------------------------------+

	+-------------- Type Entry --------------------------------------------+
	| EF         | uint8   | Entry start
	| 0000       | uint16  | Type ID
	| 0000       | uint16  | Size of type in bytes
	| 00         | uint8   | Number of child types (members in a struct)
	| 0000       | uint16  | Type ID of a child type
	|
	| ... Remaining child types ...
	|
	| BE         | uint8   | Entry end
	+----------------------------------------------------------------------+

	+-------------- Type Entry --------------------------------------------+
	| ...                                                                  |

	... Remaining Type Entries ...

	+-------------- Data Section Header -----------------------------------+
	| .DAT       | int8[4] | Start of data section
	| 0000 0000  | uint32  | Length of data section
	+----------------------------------------------------------------------+

	+-------------- Data Entry --------------------------------------------+
	| B0         | uint8   | Data entry start
	| 0000       | uint16  | Type ID
	| 0000 0000  | uint32  | Name Hash (name checks are disabled if zero)
	|
	| ... Data for all members ...
	|
	| 0B         | uint8   | Data entry end
	+----------------------------------------------------------------------+

	+-------------- Data Entry --------------------------------------------+
	| ...                                                                  |

	... Remaining Data Entries ...
*/

FILE* FxFileOpen(const char* filename, const char* mode)
{
#ifdef fopen_s
    FILE* fp = nullptr;
    fopen_s(&fp, filename, mode);
    return fp;
#endif

    return fopen(filename, mode);
}

/** Creates a new context that will call the given function at the end of scope */
template <typename FuncType>
class FxDeferObject
{
public:
    FxDeferObject(FuncType&& func) noexcept
        : mFunc(std::move(func))
    {
    }

    FxDeferObject(const FxDeferObject& other) = delete;
    FxDeferObject& operator = (const FxDeferObject& other) = delete;

    ~FxDeferObject() noexcept
    {
        mFunc();
    }

private:
    FuncType mFunc;
};

#define FX_CONCAT_INNER(a_, b_) a_##b_
#define FX_CONCAT(a_, b_) FX_CONCAT_INNER(a_, b_)

#define FxDefer(fn_) FxDeferObject FX_CONCAT(_ds_, __LINE__)(fn_)

struct FxSerializerIO;

static uint16 FxSerializeTypeIdCount = 1;

template <typename T>
inline uint16 FxSerializeGetTypeId_()
{
	static uint16 tid = FxSerializeTypeIdCount++;
	return tid;
}

template <typename T>
inline uint16 FxSerializeGetTypeId()
{
	return FxSerializeGetTypeId_<std::remove_const_t<std::remove_reference_t<T>>>();
}

struct FxWriterSection
{
	inline void Write8(uint8 value)
	{
		assert(Index < Size);
		Data[Index++] = value;
	}

	inline void Write16(uint16 value16)
	{
		assert(Index + 2 <= Size);
		Data[Index++] = static_cast<uint8>((value16 >> 8));
		Data[Index++] = static_cast<uint8>(value16);
	}

	inline void Write32(uint32 value32)
	{
		Write16(static_cast<uint16>(value32 >> 16));
		Write16(static_cast<uint16>(value32));
	}

	inline void WriteBuffer(uint32 size, const uint8* data)
	{
		assert(Index + size <= Size);
		memcpy(Data + Index, data, size);
		Index += size;
	}

	uint8 Read8()
	{
		assert(Index + 1 <= Size);

		return Data[(Index)++];
	}

	uint16 Read16()
	{
		assert(Index + 2 <= Size);

		const uint16 hi = (static_cast<uint16>(Data[Index++])) << 8;
		const uint16 lo = Data[Index++];

		return hi | lo;
	}

	uint32 Read32()
	{
		return (static_cast<uint32>(Read16()) << 16 | static_cast<uint32>(Read16()));
	}

	void Create()
	{
		Size = 10000;
		Index = 0;
		Data = FX_ALLOC_MEM(uint8, Size);
	}

	~FxWriterSection()
	{
		FX_FREE_MEM(Data);
	}

	void PrintSection()
	{
		const uint32 max_width = 20;
		for (uint32 i = 0; i < Index; i++) {
			if (!(i % max_width)) {
				puts("");
			}

			printf("%02X\t", Data[i]);
		}
		fflush(stdout);
	}

	void PrintData(uint32 count)
	{
		const int width = 20;
		for (uint32 i = 0; i < count; i++) {
			if (!(i % width)) {
				puts("");
			}
			printf("%02X ", Data[i]);
		}
		puts("");
	}

	uint8* Data = nullptr;
	uint32 Index = 0;
	uint32 Size = 0;
};

struct FxDataSection : public FxWriterSection
{
	static const uint8 MagicHeader = 0xB0;
	static const uint8 MagicFooter = 0x0B;

	void WriteHeader(uint16 type_id, FxHash name_hash)
	{
		Write8(MagicHeader);
		Write16(type_id);
		Write32(name_hash);
	}

	void WriteFooter()
	{
		Write8(MagicFooter);
	}

	void PrintFormattedData(uint32 count)
	{
		const int width = 20;

		for (uint32 i = 0; i < count; i++) {
			/*if (i >= Index) {
				putchar('\n');
				break;
			}*/

			uint8 value = Data[i];

			if (value == MagicHeader) {
				printf("<< ");
				continue;
			}
			else if (value == MagicFooter) {
				printf(">> ");
				continue;
			}

			if (!(i % width)) {
				puts("");
			}
			printf("%02X ", Data[i]);
		}

		puts("");
	}
};

template <typename T>
concept C_IsSerializable = requires(T t, FxSerializerIO& writer)
{
	{ t.WriteTo(0, writer) };
};

struct FxSerializedType
{
	uint16 Id;
	uint16 Size;
	std::vector<FxSerializedType> Members;
};

struct FxTypeSection : public FxWriterSection
{
	static const uint8 MagicHeader = 0xEF;
	static const uint8 MagicFooter = 0xBE;

	struct TypeEntry
	{
		uint16 Id;
		uint32 Offset;
	};

	std::vector<TypeEntry> mRegisteredTypeIds;

	bool IsTypePreviouslyWritten(uint16 type_id)
	{
		for (TypeEntry& tp : mRegisteredTypeIds) {
			if (tp.Id == type_id) {
				// Type is already written, skip
				return true;
			}
		}
		return false;
	}

	uint32 FindIndexFromTypeId(uint16 id)
	{
		uint32 old_index = Index;
		Index = 0;

		uint32 entry_index = 0;

		bool found_id = false;

		while (!found_id && Index < Size) {

			entry_index = Index;

			uint8 sanity_header = Read8();
			if (sanity_header != MagicHeader) {
				printf("Sanity header error when searching for Type ID %04x (%02X != %02X)\n", id, sanity_header, MagicHeader);
				break;
			}

			uint16 type_id = Read16();

			// We found the id, break to return the index for the start of the entry
			if (type_id == id) {
				break;
			}

			Read16(); // size

			uint8 number_of_members = Read8();
			for (int i = 0; i < number_of_members; i++) {
				Read16(); // size
				Read16(); // type id
			}

			uint8 sanity_footer = Read8();

			if (sanity_footer != MagicFooter) {
				printf("Sanity footer error when searching for Type ID\n");
				break;
			}

			// Continue to next entry...
		}

		Index = old_index;

		return entry_index;
	}

	FxSerializedType ReadType(uint32 index)
	{
		uint32 old_index = Index;
		Index = index;

		uint8 sanity_header = Read8();
		if (sanity_header != MagicHeader) {
			printf("Sanity header does not match!\n");
			Index = old_index;
			return {};
		}

		FxSerializedType type;
		type.Id = Read16();
		type.Size = Read16();

		uint8 number_of_members = Read8();

		for (int i = 0; i < number_of_members; i++) {
			Read16(); // size of member

			uint16 member_id = Read16();
			uint32 member_index = FindIndexFromTypeId(member_id);

			// Push the member to the type
			FxSerializedType member = ReadType(member_index);
			type.Members.emplace_back(member);
		}

		uint8 sanity_footer = Read8();
		if (sanity_footer != MagicFooter) {
			printf("Sanity footer does not match!\n");
		}

		Index = old_index;

		return type;
	}

	template <typename T>
	void WriteTypeForTypeId(FxSerializerIO& writer)
	{
		const uint16 type_id = FxSerializeGetTypeId<T>();

		if (IsTypePreviouslyWritten(type_id)) {
			return;
		}

		if constexpr (C_IsSerializable<T>) {
			T t_instance{ };
			t_instance.WriteTypeTo(writer);
		}
		else {
			WriteTypeWithoutChecks(type_id, sizeof(T));
		}

	}

	void WriteMember(uint16 type_id, uint16 size)
	{
		Write16(size);
		Write16(type_id);
	}

	template <typename... Types>
	void WriteTypeWithoutChecks(uint16 type_id, uint16 type_size, Types&&... args)
	{
		if (IsTypePreviouslyWritten(type_id)) {
			return;
		}

		printf("Writing Type %d\n", type_id);
		const uint32 start_offset = Index;

		// Write start
		Write8(MagicHeader);

		// Write the type's id
		Write16(type_id);

		// Write the size of the type
		Write16(type_size);

		// Number of member primitives
		Write8(sizeof...(args));

		// Write all of the member type id's out
		(WriteMember(FxSerializeGetTypeId<decltype(args)>(), sizeof(decltype(args))), ...);

		// Write end
		Write8(MagicFooter);

		mRegisteredTypeIds.emplace_back(TypeEntry{ type_id, start_offset });
	}

	template <typename... Types>
	void WriteType(FxSerializerIO& writer, uint16 type_id, uint16 type_size, Types&&... args)
	{
		//((std::cout << "MEMBERTYPE: " << typeid(args).name() << '\n'), ...);

		if (IsTypePreviouslyWritten(type_id)) {
			return;
		}
		// Check to see if the types exist already
		(WriteTypeForTypeId<std::remove_reference_t<decltype(args)>>(writer), ...);

		WriteTypeWithoutChecks(type_id, type_size, std::forward<Types>(args)...);
	}

	void PrintAllTypes()
	{
		printf("\n=== Types(%zu) ===\n", mRegisteredTypeIds.size());
		for (TypeEntry& entry : mRegisteredTypeIds) {
			PrintType(entry.Offset);
		}
	}

	void PrintType(uint32 index)
	{
		Index = index;

		const uint8 start_sanity = Read8();
		if (start_sanity != MagicHeader) {
			printf("Start sanity is incorrect! %X != %X\n", start_sanity, MagicHeader);
			return;
		}

		printf("Type (Sz=%d, Type=%d)\n", Read16(), Read16());

		const uint8 num_members = Read8();

		for (int i = 0; i < num_members; i++) {
			const uint16 member_size = Read16();
			const uint16 member_type_id = Read16();
			printf("\tMember Type ID: %d (size: %d)\n", member_type_id, member_size);
		}

		const uint8 end_sanity = Read8();
		if (end_sanity != MagicFooter) {
			printf("End sanity is incorrect! %x != %x\n", end_sanity, MagicFooter);
			return;
		}
	}

	void PrintFormattedData(uint32 count)
	{
		const int width = 20;

		for (uint32 i = 0; i < count; i++) {
			/*if (i >= Index) {
				break;
			}*/

			uint8 value = Data[i];

			if (value == MagicHeader) {
				printf("<< ");
				continue;
			}
			else if (value == MagicFooter) {
				printf(">> ");
				continue;
			}

			if (!(i % width)) {
				puts("");
			}
			printf("%02X ", Data[i]);
		}
		puts("");
	}
};

#define FX_SERIALIZER_IO_FILE_SIGNATURE 'FXSD'
#define FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE '.DAT'

struct FxSerializerIO
{
	FxSerializerIO()
	{
		TypeSection.Create();
		DataSection.Create();
	}

	FxTypeSection TypeSection;
	FxDataSection DataSection;


	void PrintReadableEntry(uint32 start_index)
	{
		uint32 old_index = DataSection.Index;
		DataSection.Index = start_index;

		printf("\nMagic Start: "); PrintBinaryValue(DataSection.Read8());
		const uint16 type_id = DataSection.Read16();
		printf("\nType ID    : "); PrintBinaryValue(type_id);
		printf("\nName Hash  : "); PrintBinaryValue(DataSection.Read32());
		puts("");

		FxSerializedType entry_type = TypeSection.ReadType(TypeSection.FindIndexFromTypeId(type_id));

		printf("Type {Sz:%d, Members: %zu}\n", entry_type.Size, entry_type.Members.size());

		uint32 total_members_size = 0;

		for (FxSerializedType& member : entry_type.Members) {
			printf("Member(%d, Sz: %d)\n", member.Id, member.Size);
			total_members_size += member.Size;
		}
		printf("Total size of members: %u\n", total_members_size);
		DataSection.Index += total_members_size;

		printf("Magic End: "); PrintBinaryValue(DataSection.Read8());

		DataSection.Index = old_index;
	}



	void WriteToFile(const char* filename)
	{

		FILE* fp = FxFileOpen(filename, "wb");
		if (fp == nullptr) {
			printf("Error opening file '%s' for writing!\n", filename);
			return;
		}

		uint32 signature = FX_SERIALIZER_IO_FILE_SIGNATURE;
		fwrite(&signature, sizeof(signature), 1, fp);

		// Write the size of the types section in bytes
		fwrite(&TypeSection.Index, sizeof(uint32), 1, fp);
		// Write the types section
		fwrite(TypeSection.Data, 1, TypeSection.Index, fp);

		uint32 data_signature = FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE;
		fwrite(&data_signature, sizeof(data_signature), 1, fp);

		// Write the size of the data section in bytes
		fwrite(&DataSection.Index, sizeof(uint32), 1, fp);
		// Write the data section
		fwrite(DataSection.Data, 1, DataSection.Index, fp);

		fclose(fp);
	}

	void ReadFromFile(const char* filename)
	{
		FILE* fp = FxFileOpen(filename, "rb");
		if (fp == nullptr) {
			printf("Could not open file\n");
			return;
		}

		fseek(fp, 0, SEEK_END);

		unsigned long size = ftell(fp);
		rewind(fp);

		FxDefer([&fp] {
			fclose(fp);
		});

		{
			uint32 signature_buffer = 0;
			fread(&signature_buffer, sizeof(uint32), 1, fp);

			const uint32 expected_signature = FX_SERIALIZER_IO_FILE_SIGNATURE;

			if (signature_buffer != expected_signature) {
				printf("File signature is incorrect!\n");
				return;
			}

			// Read in the size of the types section
			uint32 size_of_types;
			fread(&size_of_types, sizeof(uint32), 1, fp);
			printf("Size of types: %u\n", size_of_types);

			// Read in the types
			fread(TypeSection.Data, 1, size_of_types, fp);
		}
		{
			uint32 signature_buffer = 0;

			// Read in the data signature
			fread(&signature_buffer, sizeof(uint32), 1, fp);

			const uint32 expected_data_signature = FX_SERIALIZER_IO_SECTION_DATA_SIGNATURE;
			if (signature_buffer != expected_data_signature) {
				printf("File data signature is incorrect!\n");
				return;
			}

			uint32 size_of_data;
			fread(&size_of_data, sizeof(uint32), 1, fp);

			fread(DataSection.Data, 1, size_of_data, fp);
		}
	}


private:
	void PrintBinaryValue(uint8 value)
	{
		printf("%02X ", value);
	}
	void PrintBinaryValue(uint16 value)
	{
		PrintBinaryValue(static_cast<uint8>(value >> 8));
		PrintBinaryValue(static_cast<uint8>(value));
	}
	void PrintBinaryValue(uint32 value)
	{
		PrintBinaryValue(static_cast<uint16>(value >> 16));
		PrintBinaryValue(static_cast<uint16>(value));
	}
};

template <typename T>
void FxSerializeValue(FxSerializerIO& writer, const T& value)
{
	std::cout << "Type " << typeid(T).name() << " is not serializable!\n";
}

template <typename... Types>
constexpr void FxSerializeStruct(FxSerializerIO& writer, uint16 type_id, FxHash name_hash, Types... members)
{
	FxDataSection& data = writer.DataSection;
	data.WriteHeader(type_id, name_hash);
	(FxSerializeValue<decltype(members)>(writer, members), ...);
	data.WriteFooter();
}

template <typename T>
void FxDeserializeValue(FxSerializerIO& writer, T dest)
{
	std::cout << "Type " << typeid(T).name() << " is not deserializable!\n";
}

template <typename Type>
// Note that std::remove_cvref_t won't work here, this order is important!
using T_ExtractBarePtrType = std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<Type>>>*;

template <typename... Types>
constexpr void FxDeserializeStruct(FxSerializerIO& writer, FxHash name_hash, std::tuple<Types...> members)
{
	FxDataSection& data = writer.DataSection;

	uint8 temp;
	temp = data.Read8();
	if (temp != FxDataSection::MagicHeader) {
		printf("Header is incorrect! %02X != %02X\n", temp, FxDataSection::MagicHeader);
		return;
	}

	uint16 type_id = data.Read16();

	uint32 struct_hash = data.Read32();
	if (struct_hash && struct_hash != name_hash) {
		printf("Name hashes are not equal!\n");
		return;
	}

	//(FxDeserializeValue<decltype(members)>(writer, members), ...);
	std::apply(
		[&writer](auto&&... v)
		{
			(FxDeserializeValue(writer, const_cast<T_ExtractBarePtrType<decltype(v)>>(v)), ...);
		},
		members
	);

	temp = data.Read8();
	if (temp != FxDataSection::MagicFooter) {
		printf("Footer is incorrect!\n");
		return;
	}
}

/////////////////////////////////////
// FxSerializeValue specializations
/////////////////////////////////////

/**
 * Serializes a structure and writes to the SerializerIO `writer`.
 */
template <typename T> requires C_IsSerializable<T>
void FxSerializeValue(FxSerializerIO& writer, const T& value)
{
	value.WriteTo(0, writer);
}

template <>
void FxSerializeValue(FxSerializerIO& writer, const int32& value)
{
	//printf("Found serializable int %d\n", value);

	writer.DataSection.Write32(static_cast<uint32>(value));
}

template <>
void FxSerializeValue(FxSerializerIO& writer, const std::string& value)
{
	std::cout << "Serializing string " << value << "\n";

	const uint32 str_size = value.size();

	writer.DataSection.Write16(str_size);
	writer.DataSection.WriteBuffer(str_size, reinterpret_cast<const uint8_t*>(value.c_str()));
}

template <>
void FxSerializeValue(FxSerializerIO& writer, const float32& value)
{
	printf("Found serializable float %f\n", value);

	writer.DataSection.Write32(static_cast<uint32>(value));
}

template <>
void FxDeserializeValue(FxSerializerIO& writer, int32* value)
{
	(*value) = writer.DataSection.Read32();
}

template <>
void FxDeserializeValue(FxSerializerIO& writer, float32* value)
{
	(*value) = writer.DataSection.Read32();
}

template <typename T> requires C_IsSerializable<T>
void FxDeserializeValue(FxSerializerIO& writer, T* value)
{
	value->ReadFrom(0, writer);
	//(*value) = writer.DataSection.Read32();
}

/**
 * Returns a tuple containing a pointer to all `args` passed in.
 * For example:
 * int x; float y;
 * FxValuesToPtrsTuple(x, y) -> std::tuple<int*, float*>();
 */
template <typename... Types>
auto FxValuesToPtrsTuple(Types&... args)
{
	return std::tuple<Types*...>{ &args... };
}

#define FX_SERIALIZABLE_MEMBERS(...) \
	const uint16 SerializerTypeId_ = FxSerializeGetTypeId<decltype(*this)>(); \
	void WriteTypeTo(FxSerializerIO& writer) const \
	{ \
		writer.TypeSection.WriteType(writer, SerializerTypeId_, sizeof(decltype(*this)), __VA_ARGS__); \
	} \
	void WriteTo(FxHash name_hash, FxSerializerIO& writer) const \
	{ \
		WriteTypeTo(writer); \
		FxSerializeStruct(writer, SerializerTypeId_, name_hash, __VA_ARGS__); \
	} \
	void ReadFrom(FxHash name_hash, FxSerializerIO& writer) const \
	{ \
		FxDeserializeStruct(writer, name_hash, FxValuesToPtrsTuple(__VA_ARGS__)); \
	}

struct TestStructB
{
	int32 A = 5;
	int32 B = 10;

	//std::string Str;

	FX_SERIALIZABLE_MEMBERS(A, B);
};

struct TestStructA
{
	int32 X = 30;
	int32 Y = 15;
	float32 Z = 3;

	TestStructB Other;

	// Serializes X, Y, Z, and then serializes the `other` struct
	FX_SERIALIZABLE_MEMBERS(X, Y, Z, Other);
};

void ReadSerializedData(FxSerializerIO& writer)
{
	puts("Data: ");
	writer.DataSection.PrintFormattedData(100);
	writer.TypeSection.PrintFormattedData(100);

	writer.DataSection.Index = 0;

	TestStructA data{};
	data.ReadFrom(FxHashStr("TestStructA"), writer);

	printf("Values: {%d, %d, %f}, other.B = %d\n", data.X, data.Y, data.Z, data.Other.B);

	printf("\nDebugEntry:\n");
	writer.PrintReadableEntry(0);
}

void WriteSerializedData(FxSerializerIO& writer)
{
	TestStructA data{};
	data.WriteTo(FxHashStr("TestStructA"), writer);
}




int main()
{
#ifdef FX_USE_MEMPOOL
	FxMemPool::GetGlobalPool().Create(1000);
#endif

	FxSerializerIO test_writer;

	WriteSerializedData(test_writer);

	test_writer.WriteToFile("Test.fxsd");

	printf("\nReading serialized values...\n");


	FxSerializerIO test_reader;
	test_reader.ReadFromFile("Test.fxsd");

	ReadSerializedData(test_reader);

	test_writer.TypeSection.PrintAllTypes();

	return 0;
}
