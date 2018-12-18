/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Purchasing module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3-COMM$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwininapppurchasebackend_p.h"
#include "qwinstorebridge.h"
#include "qwininappproduct_p.h"
#include "qwininapptransaction_p.h"
#include "qinappstore.h"
#include <QJsonDocument>
#include <QMap>
#include <QDebug>
#include <QWidget>

using namespace winrt::Windows::Services::Store;
QT_BEGIN_NAMESPACE

inline bool compareProductTypes(QInAppProduct::ProductType qtType, QString nativeType)
{
    if (qtType == QInAppProduct::Consumable && (nativeType == "Consumable"
                                                || nativeType == "UnmanagedConsumable"))
        return true;
    else if (qtType == QInAppProduct::Unlockable && (nativeType == "Durable"
                                                     || nativeType == "Application"))
        return true;
    else
        return false;
}
inline QInAppProduct::ProductType translateProductTypes(QString nativeType)
{
    QInAppProduct::ProductType qtType;
    if (nativeType == "Consumable" || nativeType == "UnmanagedConsumable")
        return qtType = QInAppProduct::ProductType::Consumable;
    else
        return qtType = QInAppProduct::ProductType::Unlockable;
}

inline QString hStringToQString(const winrt::hstring &h)
{
    unsigned int length;
    const wchar_t *raw = h.c_str();
    return QString::fromStdWString(raw);
}


class QWinInAppPurchaseBackendPrivate : public QObject
{

public:
    explicit QWinInAppPurchaseBackendPrivate(QWinInAppPurchaseBackend *p)
        : QObject(p), q_ptr(p), m_storeContext(0)
    { }

    QWinInAppPurchaseBackend *q_ptr;
    QMap<QString, QWinInAppProduct *> nativeProducts;

    bool m_waitingForList = false;
    StoreContext m_storeContext;
    WId window;
    bool initWindow = true;

public slots:
    void addToNativeProducts(const QString &);
    void nativeListReady();


    Q_DECLARE_PUBLIC(QWinInAppPurchaseBackend)
};

void QWinInAppPurchaseBackendPrivate::addToNativeProducts(const QString &product)
{

    QJsonDocument p = QJsonDocument::fromJson(product.toUtf8());
    QString price = "";
    QString identifier = "";
    if (p["ProductKind"].toString() == "Application") {
        identifier = p["ProductId"].toString();
        price = "0";
    } else {
        price = p["Availabilities"]["OrderManagementData"]["Price"]["ListPrice"].toString();
        identifier = p["Properties"]["InAppOfferToken"].toString();
    }

    QString name = p["LocalizedProperties"][0]["ProductTitle"].toString();
    QInAppProduct::ProductType productType = translateProductTypes(p["ProductKind"].toString());

    QWinInAppProduct *appProduct = new QWinInAppProduct((QWinInAppPurchaseBackend *)this->parent(),
                                                        price, name, QString(), productType, identifier, this->parent());
    appProduct->storeID = p["ProductId"].toString();
    appProduct->productKind = p["ProductKind"].toString();


    if (!nativeProducts.contains(identifier)) {
        nativeProducts.insert(identifier, appProduct);
    }
}

void QWinInAppPurchaseBackendPrivate::nativeListReady()
{
    m_waitingForList = false;
}

QWinInAppPurchaseBackend::QWinInAppPurchaseBackend(QObject *parent)
    : QInAppPurchaseBackend(parent)
{
    d_ptr.reset(new QWinInAppPurchaseBackendPrivate(this));
}

void QWinInAppPurchaseBackend::initialize()
{
    Q_D(QWinInAppPurchaseBackend);
    setContext();
    d->m_waitingForList = true;
    WinStoreBridge *storeBridge = new WinStoreBridge();
    storeBridge->setAutoDelete(true);
    storeBridge->setContext(d->m_storeContext);
    storeBridge->setOperation(WinStoreBridge::mType::getAppInfo);
    QThreadPool::globalInstance()->tryStart(storeBridge);
    connect(storeBridge, &WinStoreBridge::storeProduct, d,
            &QWinInAppPurchaseBackendPrivate::addToNativeProducts);
    connect(storeBridge, &WinStoreBridge::requestDone, d,
            &QWinInAppPurchaseBackendPrivate::nativeListReady);
    connect(storeBridge, &WinStoreBridge::requestDone, this, &QWinInAppPurchaseBackend::ready);
}

bool QWinInAppPurchaseBackend::isReady() const
{
    Q_D(const QWinInAppPurchaseBackend);
    return !d->m_waitingForList && !d->nativeProducts.isEmpty();
}

void QWinInAppPurchaseBackend::queryProducts(const QList<Product> &products)
{
    for (const Product &product : products)
        queryProduct(product.productType, product.identifier);
}

void QWinInAppPurchaseBackend::queryProduct(QInAppProduct::ProductType productType,
                                            const QString &identifier)
{
    Q_D(const QWinInAppPurchaseBackend);


    if (!d->nativeProducts.contains(identifier)
            || !compareProductTypes(productType, d->nativeProducts.value(identifier)->productKind)) {
        emit productQueryFailed(productType, identifier);
        return;
    }


    emit productQueryDone(d->nativeProducts.value(identifier));
}

void QWinInAppPurchaseBackend::restorePurchases()
{
    Q_D(QWinInAppPurchaseBackend);
    setContext();

    WinStoreBridge *storeBridge = new WinStoreBridge();
    storeBridge->setAutoDelete(true);
    storeBridge->setContext(d->m_storeContext);
    storeBridge->setOperation(WinStoreBridge::mType::checkIsTrial);
    QThreadPool::globalInstance()->tryStart(storeBridge);
    connect(storeBridge, &WinStoreBridge::isAppActive, this, &QWinInAppPurchaseBackend::isAppActive);
    connect(storeBridge, &WinStoreBridge::isAddonActive, this,
            &QWinInAppPurchaseBackend::isAddonActive);

}


void QWinInAppPurchaseBackend::setPlatformProperty(const QString &propertyName,
                                                   const QString &value)
{
    Q_D(QWinInAppPurchaseBackend);
    if (propertyName == QLatin1String("window")) {
        d->window = value.toInt();
    }
}

void QWinInAppPurchaseBackend::purchaseProduct(QWinInAppProduct *product)
{
    Q_D(QWinInAppPurchaseBackend);
    setContext();

    WinStoreBridge *storeBridge = new WinStoreBridge();
    storeBridge->setAutoDelete(true);
    storeBridge->setContext(d->m_storeContext);
    storeBridge->setOperation(WinStoreBridge::mType::buyProduct);
    storeBridge->setProduct(product);
    QThreadPool::globalInstance()->tryStart(storeBridge);
    connect(storeBridge, &WinStoreBridge::purchaseSuccess, this,
            &QWinInAppPurchaseBackend::purchaseSuccess);
    connect(storeBridge, &WinStoreBridge::purchaseCanceled, this,
            &QWinInAppPurchaseBackend::purchaseCanceled);
    connect(storeBridge, &WinStoreBridge::purchaseFailed, this,
            &QWinInAppPurchaseBackend::purchaseFailed);
}

void QWinInAppPurchaseBackend::fulfillConsumable(QWinInAppTransaction *transaction)
{
    Q_D(QWinInAppPurchaseBackend);
    setContext();

    WinStoreBridge *storeBridge = new WinStoreBridge();
    storeBridge->setAutoDelete(true);
    storeBridge->setContext(d->m_storeContext);
    storeBridge->setOperation(WinStoreBridge::mType::fulfillConsumable);
    storeBridge->setProduct((QWinInAppProduct *)transaction->product());
    QThreadPool::globalInstance()->tryStart(storeBridge);
}

void QWinInAppPurchaseBackend::setContext()
{
    Q_D(QWinInAppPurchaseBackend);
    if (d->m_storeContext == nullptr) {
        auto factory =
            winrt::get_activation_factory<winrt::Windows::Services::Store::StoreContext, winrt::Windows::Services::Store::IStoreContextStatics>();
        d->m_storeContext = factory.GetDefault();
    }
    if (d->initWindow && d->window) {
        winrt::com_ptr<IInitializeWithWindow> initWindow;
        winrt::Windows::Foundation::IUnknown *unknown =
            reinterpret_cast<winrt::Windows::Foundation::IUnknown *>(&d->m_storeContext);
        unknown->as(initWindow);
        initWindow->Initialize(HWND(d->window));
        initWindow = false;
    }

}

void QWinInAppPurchaseBackend::isAppActive(const QString &jsondata)
{
    Q_D(const QWinInAppPurchaseBackend);
    QJsonDocument p = QJsonDocument::fromJson(jsondata.toUtf8());
    qDebug() << p["productId"].toString();
    if (d->nativeProducts.contains(p["productId"].toString())) {
        QInAppProduct *product = nullptr;
        if (p["isActive"].toBool() && !p["isTrial"].toBool()) {
            product = store()->registeredProduct(p["productId"].toString());

            if (!product) {
                queryProduct(QInAppProduct::Unlockable, p["productId"].toString());
                product = store()->registeredProduct(p["productId"].toString());
            }

            auto transaction = new QWinInAppTransaction(QInAppTransaction::PurchaseRestored,
                                                        product,
                                                        QInAppTransaction::NoFailure,
                                                        p["expiration"].toString(),
                                                        this);
            emit transactionReady(transaction);
        }
    }
}

void QWinInAppPurchaseBackend::isAddonActive(const QString &jsondata)
{
    Q_D(const QWinInAppPurchaseBackend);
    QJsonDocument p = QJsonDocument::fromJson(jsondata.toUtf8());
    if (d->nativeProducts.contains(p["inAppOfferToken"].toString())) {
        QInAppProduct *product = nullptr;
        if (p["isActive"].toBool()) {
            product = store()->registeredProduct(p["inAppOfferToken"].toString());
        }
        if (!product) {
            queryProduct(QInAppProduct::Unlockable, p["inAppOfferToken"].toString());
            product = store()->registeredProduct(p["inAppOfferToken"].toString());
        }

        auto transaction = new QWinInAppTransaction(QInAppTransaction::PurchaseRestored,
                                                    product,
                                                    QInAppTransaction::NoFailure,
                                                    p["expiration"].toString(),
                                                    this);
        emit transactionReady(transaction);
    }
}

void QWinInAppPurchaseBackend::purchaseSuccess(const QString &product, const QString &message)
{
    QInAppProduct *p = store()->registeredProduct(product);
    QWinInAppTransaction *transaction = new QWinInAppTransaction(
        QInAppTransaction::TransactionStatus::PurchaseApproved,
        p,
        QInAppTransaction::FailureReason::NoFailure,
        QStringLiteral(""),
        this);
    transaction->m_extendedError = message;
    emit transactionReady(transaction);
}
void QWinInAppPurchaseBackend::purchaseCanceled(const QString &product, const QString &message)
{
    QInAppProduct *p = store()->registeredProduct(product);
    QWinInAppTransaction *transaction = new QWinInAppTransaction(
        QInAppTransaction::TransactionStatus::PurchaseFailed,
        p,
        QInAppTransaction::FailureReason::CanceledByUser,
        QStringLiteral(""),
        this);
    transaction->m_extendedError = message;
    emit transactionReady(transaction);
}

void QWinInAppPurchaseBackend::purchaseFailed(const QString &product, const QString &message)
{
    QInAppProduct *p = store()->registeredProduct(product);
    QWinInAppTransaction *transaction = new QWinInAppTransaction(
        QInAppTransaction::TransactionStatus::PurchaseApproved,
        p,
        QInAppTransaction::FailureReason::ErrorOccurred,
        QStringLiteral(""),
        this);
    transaction->m_extendedError = message;
    emit transactionReady(transaction);
}
QT_END_NAMESPACE

