// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "include_base_utils.h"
using namespace epee;

#include "wallet_rpc_server.h"
#include "common/command_line.h"
#include "cryptonote_core/cryptonote_format_utils.h"
#include "cryptonote_core/account.h"
#include "misc_language.h"
#include "string_tools.h"
#include "crypto/hash.h"
#include "WalletHelper.h"
#include "wallet_errors.h"


using namespace CryptoNote;
using namespace cryptonote;
namespace tools {
//-----------------------------------------------------------------------------------
const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_bind_port = { "rpc-bind-port", "Starts wallet as rpc server for wallet operations, sets bind port for server", "", true };
const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_bind_ip = { "rpc-bind-ip", "Specify ip to bind rpc server", "127.0.0.1" };

void wallet_rpc_server::init_options(boost::program_options::options_description& desc) {
  command_line::add_arg(desc, arg_rpc_bind_ip);
  command_line::add_arg(desc, arg_rpc_bind_port);
}
//------------------------------------------------------------------------------------------------------------------------------
wallet_rpc_server::wallet_rpc_server(std::unique_ptr<CryptoNote::IWallet>& w, CryptoNote::INode& n, cryptonote::Currency& currency, const std::string& walletFile, const password_container& pass) : m_wallet(w), m_node(n), m_currency(currency), m_walletFilename(walletFile), m_pass(pass) {
  assert(m_wallet.get() != nullptr);
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::run() {
  //DO NOT START THIS SERVER IN MORE THEN 1 THREADS WITHOUT REFACTORING
  return epee::http_server_impl_base<wallet_rpc_server, connection_context>::run(1, true);
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::handle_command_line(const boost::program_options::variables_map& vm) {
  m_bind_ip = command_line::get_arg(vm, arg_rpc_bind_ip);
  m_port = command_line::get_arg(vm, arg_rpc_bind_port);
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::init(const boost::program_options::variables_map& vm) {
  m_net_server.set_threads_prefix("RPC");
  bool r = handle_command_line(vm);
  CHECK_AND_ASSERT_MES(r, false, "Failed to process command line in core_rpc_server");
  return epee::http_server_impl_base<wallet_rpc_server, connection_context>::init(m_port, m_bind_ip);
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_getbalance(const wallet_rpc::COMMAND_RPC_GET_BALANCE::request& req, wallet_rpc::COMMAND_RPC_GET_BALANCE::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  try {
    res.locked_amount = m_wallet->pendingBalance();
    res.available_balance = m_wallet->actualBalance();
    res.balance = res.locked_amount + res.available_balance;
    res.unlocked_balance = res.available_balance;
  } catch (std::exception& e) {
    er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
    er.message = e.what();
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_transfer(const wallet_rpc::COMMAND_RPC_TRANSFER::request& req, wallet_rpc::COMMAND_RPC_TRANSFER::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  std::vector<CryptoNote::Transfer> transfers;
  for (auto it = req.destinations.begin(); it != req.destinations.end(); it++) {
    CryptoNote::Transfer transfer;
    transfer.address = it->address;
    transfer.amount = it->amount;
    transfers.push_back(transfer);
  }

  std::vector<uint8_t> extra;
  if (!req.payment_id.empty()) {
    std::string payment_id_str = req.payment_id;

    crypto::hash payment_id;
    if (!cryptonote::parsePaymentId(payment_id_str, payment_id)) {
      er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
      er.message = "Payment id has invalid format: \"" + payment_id_str + "\", expected 64-character string";
      return false;
    }

    std::string extra_nonce;
    cryptonote::set_payment_id_to_tx_extra_nonce(extra_nonce, payment_id);
    if (!cryptonote::add_extra_nonce_to_tx_extra(extra, extra_nonce)) {
      er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
      er.message = "Something went wrong with payment_id. Please check its format: \"" + payment_id_str + "\", expected 64-character string";
      return false;
    }
  }

  std::vector<CryptoNote::TransactionMessage> messages;
  for (auto& rpc_message : req.messages) {
     messages.emplace_back(CryptoNote::TransactionMessage{ rpc_message.message, rpc_message.address });
  }

  std::string extraString;
  std::copy(extra.begin(), extra.end(), std::back_inserter(extraString));
  try {
    cryptonote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    CryptoNote::TransactionId tx = m_wallet->sendTransaction(transfers, req.fee, extraString, req.mixin, req.unlock_time, messages);
    if (tx == INVALID_TRANSACTION_ID) {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = "WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR";
      return false;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();
    if (sendError) {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = sendError.message();
      return false;
    }

    CryptoNote::TransactionInfo txInfo;
    m_wallet->getTransaction(tx, txInfo);

    std::string hexHash;
    std::copy(txInfo.hash.begin(), txInfo.hash.end(), std::back_inserter(hexHash));
    res.tx_hash = epee::string_tools::buff_to_hex_nodelimer(hexHash);
    return true;
  } catch (const tools::error::daemon_busy& e) {
    er.code = WALLET_RPC_ERROR_CODE_DAEMON_IS_BUSY;
    er.message = e.what();
    return false;
  } catch (const std::exception& e) {
    er.code = WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR;
    er.message = e.what();
    return false;
  } catch (...) {
    er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
    er.message = "WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR";
    return false;
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_store(const wallet_rpc::COMMAND_RPC_STORE::request& req, wallet_rpc::COMMAND_RPC_STORE::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  try {
    WalletHelper::storeWallet(*m_wallet, m_walletFilename);
  } catch (std::exception& e) {
    er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
    er.message = e.what();
    return false;
  }

  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_get_payments(const wallet_rpc::COMMAND_RPC_GET_PAYMENTS::request& req, wallet_rpc::COMMAND_RPC_GET_PAYMENTS::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  PaymentId expectedPaymentId;
  cryptonote::blobdata payment_id_blob;
  if (!epee::string_tools::parse_hexstr_to_binbuff(req.payment_id, payment_id_blob)) {
    er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
    er.message = "Payment ID has invald format";
    return false;
  }

  if (sizeof(expectedPaymentId) != payment_id_blob.size()) {
    er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
    er.message = "Payment ID has invalid size";
    return false;
  }

  std::copy(std::begin(payment_id_blob), std::end(payment_id_blob), reinterpret_cast<char*>(&expectedPaymentId)); // no UB, char can alias any type
  auto payments = m_wallet->getTransactionsByPaymentIds({expectedPaymentId});
  assert(payments.size() == 1);
  for (auto& transaction : payments[0].transactions) {
    wallet_rpc::payment_details rpc_payment;
    rpc_payment.tx_hash = epee::string_tools::pod_to_hex(transaction.hash);
    rpc_payment.amount = transaction.totalAmount;
    rpc_payment.block_height = transaction.blockHeight;
    rpc_payment.unlock_time = transaction.unlockTime;
    res.payments.push_back(rpc_payment);
  }
  return true;
}

bool wallet_rpc_server::on_get_transfers(const wallet_rpc::COMMAND_RPC_GET_TRANSFERS::request& req, wallet_rpc::COMMAND_RPC_GET_TRANSFERS::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  res.transfers.clear();
  size_t transactionsCount = m_wallet->getTransactionCount();
  for (size_t trantransactionNumber = 0; trantransactionNumber < transactionsCount; ++trantransactionNumber) {
    TransactionInfo txInfo;
    m_wallet->getTransaction(trantransactionNumber, txInfo);
    if (txInfo.state != TransactionState::Active || txInfo.blockHeight == UNCONFIRMED_TRANSACTION_HEIGHT) {
      continue;
    }

    std::string address = "";
    if (txInfo.totalAmount < 0) {
      if (txInfo.transferCount > 0) {
        Transfer tr;
        m_wallet->getTransfer(txInfo.firstTransferId, tr);
        address = tr.address;
      }
    }

    wallet_rpc::Transfer transfer;
    transfer.time = txInfo.timestamp;
    transfer.output = txInfo.totalAmount < 0;
    transfer.transactionHash = epee::string_tools::pod_to_hex(txInfo.hash);
    transfer.amount = std::abs(txInfo.totalAmount);
    transfer.fee = txInfo.fee;
    transfer.address = address;
    transfer.blockIndex = txInfo.blockHeight;
    transfer.unlockTime = txInfo.unlockTime;
    transfer.paymentId = "";

    std::vector<uint8_t> extraVec;
    extraVec.reserve(txInfo.extra.size());
    std::for_each(txInfo.extra.begin(), txInfo.extra.end(), [&extraVec](const char el) { extraVec.push_back(el); });

    crypto::hash paymentId;
    transfer.paymentId = (getPaymentIdFromTxExtra(extraVec, paymentId) && paymentId != null_hash ? epee::string_tools::pod_to_hex(paymentId) : "");

    res.transfers.push_back(transfer);
  }

  return true;
}

bool wallet_rpc_server::on_get_height(const wallet_rpc::COMMAND_RPC_GET_HEIGHT::request& req, wallet_rpc::COMMAND_RPC_GET_HEIGHT::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  res.height = m_node.getLastLocalBlockHeight();
  return true;
}

bool wallet_rpc_server::on_reset(const wallet_rpc::COMMAND_RPC_RESET::request& req, wallet_rpc::COMMAND_RPC_RESET::response& res, epee::json_rpc::error& er, connection_context& cntx) {
  if (m_pass.empty()) {
    er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
    er.message = "password not set";
    return false;
  }

  // die on exception
  try {
    std::stringstream stream;
    auto error = WalletHelper::walletSaveWrapper(*m_wallet, stream, false, false);
    if (error) {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = "failed to save wallet";
      return false;
    }

    m_wallet->shutdown();

    m_wallet.reset(new Wallet(m_currency, m_node));
    error = WalletHelper::initAndLoadWallet(*m_wallet, stream, m_pass.password());
    if (error) {
      throw std::runtime_error("failed to reinitialize wallet");
    }

  } catch (std::exception& e) {
    // kill simple wallet if something is wrong
    std::cerr << "reset rpc request failed" << std::endl;
    std::abort();
  }

  return true;
}

}
