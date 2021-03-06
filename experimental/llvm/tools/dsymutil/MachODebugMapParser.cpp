//===- tools/dsymutil/MachODebugMapParser.cpp - Parse STABS debug maps ----===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BinaryHolder.h"
#include "DebugMap.h"
#include "dsymutil.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

namespace {
using namespace llvm;
using namespace llvm::dsymutil;
using namespace llvm::object;

class MachODebugMapParser {
public:
  MachODebugMapParser(StringRef BinaryPath, ArrayRef<std::string> Archs,
                      StringRef PathPrefix = "", bool Verbose = false)
      : BinaryPath(BinaryPath), Archs(Archs.begin(), Archs.end()),
        PathPrefix(PathPrefix), MainBinaryHolder(Verbose),
        CurrentObjectHolder(Verbose), CurrentDebugMapObject(nullptr) {}

  /// \brief Parses and returns the DebugMaps of the input binary.
  /// The binary contains multiple maps in case it is a universal
  /// binary.
  /// \returns an error in case the provided BinaryPath doesn't exist
  /// or isn't of a supported type.
  ErrorOr<std::vector<std::unique_ptr<DebugMap>>> parse();

private:
  std::string BinaryPath;
  SmallVector<StringRef, 1> Archs;
  std::string PathPrefix;

  /// Owns the MemoryBuffer for the main binary.
  BinaryHolder MainBinaryHolder;
  /// Map of the binary symbol addresses.
  StringMap<uint64_t> MainBinarySymbolAddresses;
  StringRef MainBinaryStrings;
  /// The constructed DebugMap.
  std::unique_ptr<DebugMap> Result;

  /// Owns the MemoryBuffer for the currently handled object file.
  BinaryHolder CurrentObjectHolder;
  /// Map of the currently processed object file symbol addresses.
  StringMap<uint64_t> CurrentObjectAddresses;
  /// Element of the debug map corresponfing to the current object file.
  DebugMapObject *CurrentDebugMapObject;

  /// Holds function info while function scope processing.
  const char *CurrentFunctionName;
  uint64_t CurrentFunctionAddress;

  std::unique_ptr<DebugMap> parseOneBinary(const MachOObjectFile &MainBinary,
                                           StringRef BinaryPath);

  void switchToNewDebugMapObject(StringRef Filename, sys::TimeValue Timestamp);
  void resetParserState();
  uint64_t getMainBinarySymbolAddress(StringRef Name);
  void loadMainBinarySymbols(const MachOObjectFile &MainBinary);
  void loadCurrentObjectFileSymbols(const object::MachOObjectFile &Obj);
  void handleStabSymbolTableEntry(uint32_t StringIndex, uint8_t Type,
                                  uint8_t SectionIndex, uint16_t Flags,
                                  uint64_t Value);

  template <typename STEType> void handleStabDebugMapEntry(const STEType &STE) {
    handleStabSymbolTableEntry(STE.n_strx, STE.n_type, STE.n_sect, STE.n_desc,
                               STE.n_value);
  }
};

static void Warning(const Twine &Msg) { errs() << "warning: " + Msg + "\n"; }
}

/// Reset the parser state coresponding to the current object
/// file. This is to be called after an object file is finished
/// processing.
void MachODebugMapParser::resetParserState() {
  CurrentObjectAddresses.clear();
  CurrentDebugMapObject = nullptr;
}

/// Create a new DebugMapObject. This function resets the state of the
/// parser that was referring to the last object file and sets
/// everything up to add symbols to the new one.
void MachODebugMapParser::switchToNewDebugMapObject(StringRef Filename,
                                                    sys::TimeValue Timestamp) {
  resetParserState();

  SmallString<80> Path(PathPrefix);
  sys::path::append(Path, Filename);

  auto MachOOrError =
      CurrentObjectHolder.GetFilesAs<MachOObjectFile>(Path, Timestamp);
  if (auto Error = MachOOrError.getError()) {
    Warning(Twine("cannot open debug object \"") + Path.str() + "\": " +
            Error.message() + "\n");
    return;
  }

  auto ErrOrAchObj =
      CurrentObjectHolder.GetAs<MachOObjectFile>(Result->getTriple());
  if (auto Err = ErrOrAchObj.getError()) {
    return Warning(Twine("cannot open debug object \"") + Path.str() + "\": " +
                   Err.message() + "\n");
  }

  CurrentDebugMapObject = &Result->addDebugMapObject(Path, Timestamp);
  loadCurrentObjectFileSymbols(*ErrOrAchObj);
}

std::unique_ptr<DebugMap>
MachODebugMapParser::parseOneBinary(const MachOObjectFile &MainBinary,
                                    StringRef BinaryPath) {
  loadMainBinarySymbols(MainBinary);
  Result = make_unique<DebugMap>(BinaryHolder::getTriple(MainBinary));
  MainBinaryStrings = MainBinary.getStringTableData();
  for (const SymbolRef &Symbol : MainBinary.symbols()) {
    const DataRefImpl &DRI = Symbol.getRawDataRefImpl();
    if (MainBinary.is64Bit())
      handleStabDebugMapEntry(MainBinary.getSymbol64TableEntry(DRI));
    else
      handleStabDebugMapEntry(MainBinary.getSymbolTableEntry(DRI));
  }

  resetParserState();
  return std::move(Result);
}

static bool shouldLinkArch(SmallVectorImpl<StringRef> &Archs, StringRef Arch) {
  if (Archs.empty() ||
      std::find(Archs.begin(), Archs.end(), "all") != Archs.end() ||
      std::find(Archs.begin(), Archs.end(), "*") != Archs.end())
    return true;

  if (Arch.startswith("arm") && Arch != "arm64" &&
      std::find(Archs.begin(), Archs.end(), "arm") != Archs.end())
    return true;

  return std::find(Archs.begin(), Archs.end(), Arch) != Archs.end();
}

/// This main parsing routine tries to open the main binary and if
/// successful iterates over the STAB entries. The real parsing is
/// done in handleStabSymbolTableEntry.
ErrorOr<std::vector<std::unique_ptr<DebugMap>>> MachODebugMapParser::parse() {
  auto MainBinOrError =
      MainBinaryHolder.GetFilesAs<MachOObjectFile>(BinaryPath);
  if (auto Error = MainBinOrError.getError())
    return Error;

  std::vector<std::unique_ptr<DebugMap>> Results;
  Triple T;
  for (const auto *Binary : *MainBinOrError)
    if (shouldLinkArch(Archs, Binary->getArch(nullptr, &T).getArchName()))
      Results.push_back(parseOneBinary(*Binary, BinaryPath));

  return std::move(Results);
}

/// Interpret the STAB entries to fill the DebugMap.
void MachODebugMapParser::handleStabSymbolTableEntry(uint32_t StringIndex,
                                                     uint8_t Type,
                                                     uint8_t SectionIndex,
                                                     uint16_t Flags,
                                                     uint64_t Value) {
  if (!(Type & MachO::N_STAB))
    return;

  const char *Name = &MainBinaryStrings.data()[StringIndex];

  // An N_OSO entry represents the start of a new object file description.
  if (Type == MachO::N_OSO) {
    sys::TimeValue Timestamp;
    Timestamp.fromEpochTime(Value);
    return switchToNewDebugMapObject(Name, Timestamp);
  }

  // If the last N_OSO object file wasn't found,
  // CurrentDebugMapObject will be null. Do not update anything
  // until we find the next valid N_OSO entry.
  if (!CurrentDebugMapObject)
    return;

  uint32_t Size = 0;
  switch (Type) {
  case MachO::N_GSYM:
    // This is a global variable. We need to query the main binary
    // symbol table to find its address as it might not be in the
    // debug map (for common symbols).
    Value = getMainBinarySymbolAddress(Name);
    break;
  case MachO::N_FUN:
    // Functions are scopes in STABS. They have an end marker that
    // contains the function size.
    if (Name[0] == '\0') {
      Size = Value;
      Value = CurrentFunctionAddress;
      Name = CurrentFunctionName;
      break;
    } else {
      CurrentFunctionName = Name;
      CurrentFunctionAddress = Value;
      return;
    }
  case MachO::N_STSYM:
    break;
  default:
    return;
  }

  auto ObjectSymIt = CurrentObjectAddresses.find(Name);
  if (ObjectSymIt == CurrentObjectAddresses.end())
    return Warning("could not find object file symbol for symbol " +
                   Twine(Name));
  if (!CurrentDebugMapObject->addSymbol(Name, ObjectSymIt->getValue(), Value,
                                        Size))
    return Warning(Twine("failed to insert symbol '") + Name +
                   "' in the debug map.");
}

/// Load the current object file symbols into CurrentObjectAddresses.
void MachODebugMapParser::loadCurrentObjectFileSymbols(
    const object::MachOObjectFile &Obj) {
  CurrentObjectAddresses.clear();

  for (auto Sym : Obj.symbols()) {
    uint64_t Addr = Sym.getValue();
    ErrorOr<StringRef> Name = Sym.getName();
    if (!Name)
      continue;
    CurrentObjectAddresses[*Name] = Addr;
  }
}

/// Lookup a symbol address in the main binary symbol table. The
/// parser only needs to query common symbols, thus not every symbol's
/// address is available through this function.
uint64_t MachODebugMapParser::getMainBinarySymbolAddress(StringRef Name) {
  auto Sym = MainBinarySymbolAddresses.find(Name);
  if (Sym == MainBinarySymbolAddresses.end())
    return 0;
  return Sym->second;
}

/// Load the interesting main binary symbols' addresses into
/// MainBinarySymbolAddresses.
void MachODebugMapParser::loadMainBinarySymbols(
    const MachOObjectFile &MainBinary) {
  section_iterator Section = MainBinary.section_end();
  MainBinarySymbolAddresses.clear();
  for (const auto &Sym : MainBinary.symbols()) {
    SymbolRef::Type Type = Sym.getType();
    // Skip undefined and STAB entries.
    if ((Type & SymbolRef::ST_Debug) || (Type & SymbolRef::ST_Unknown))
      continue;
    // The only symbols of interest are the global variables. These
    // are the only ones that need to be queried because the address
    // of common data won't be described in the debug map. All other
    // addresses should be fetched for the debug map.
    if (!(Sym.getFlags() & SymbolRef::SF_Global) || Sym.getSection(Section) ||
        Section == MainBinary.section_end() || Section->isText())
      continue;
    uint64_t Addr = Sym.getValue();
    ErrorOr<StringRef> NameOrErr = Sym.getName();
    if (!NameOrErr)
      continue;
    StringRef Name = *NameOrErr;
    if (Name.size() == 0 || Name[0] == '\0')
      continue;
    MainBinarySymbolAddresses[Name] = Addr;
  }
}

namespace llvm {
namespace dsymutil {
llvm::ErrorOr<std::vector<std::unique_ptr<DebugMap>>>
parseDebugMap(StringRef InputFile, ArrayRef<std::string> Archs,
              StringRef PrependPath, bool Verbose, bool InputIsYAML) {
  if (!InputIsYAML) {
    MachODebugMapParser Parser(InputFile, Archs, PrependPath, Verbose);
    return Parser.parse();
  } else {
    return DebugMap::parseYAMLDebugMap(InputFile, PrependPath, Verbose);
  }
}
}
}
